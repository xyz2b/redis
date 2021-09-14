//
// Created by xyzjiao on 9/6/21.
//

#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "redisassert.h"
#include "endiarconv.h"
#include "ziplist.h"
#include "zmalloc.h"
#include "util.h"

// 0xc0 --> 1100 0000
// ZIP_STR编码方式的标识符仅为前两位，组合有00 01 10
#define ZIP_STR_MASK 0xc0
// 0x30 --> 0011 0000
// ZIP_INT编码方式的前两位都是11
// ZIP_INT编码方式的标识符仅为第三四位，组合有00 01 10 11
#define ZIP_INT_MASK 0x30
// ZIP_STR编码值都比ZIP_INT编码值要小，因为ZIP_INT编码值前两位都是11

// 大端存储，高位存储在低地址，低位存储在高地址
// 0000 0000  --> ZIP_STR编码标识符为00，标识字符串长度len字段的长度为1个字节，len的值在00bb bbbb后6位
#define ZIP_STR_06B (0 << 6)
// 0100 0000  --> ZIP_STR编码标识符为01，标识字符串长度len字段的长度为2个字节，len的值在01bbbbbb xxxxxxxx后14位
#define ZIP_STR_14B (1 << 6)
// 1000 0000  --> ZIP_STR编码标识符为10，标识字符串长度len字段的长度为5个字节，len的值在10______ aaaaaaaa bbbbbbbb cccccccc dddddddd后32位
#define ZIP_STR_32B (2 << 6)

// 1100 0000    --> int16_t类型的整数
#define ZIP_INT_16B (0xc0 | 0<<4)
// 1101 0000    --> int32_t类型的整数
#define ZIP_INT_32B (0xc0 | 1<<4)
// 1110 0000    --> int64_t类型的整数
#define ZIP_INT_64B (0xc0 | 2<<4)
// 1111 0000    --> 24位有符号整数
#define ZIP_INT_24B (0xc0 | 3<<4)
// 1111 1110    --> 8位有符号整数
#define ZIP_INT_8B 0xfe

// 24位有符号数的最大值
#define INT24_MAX 0x7fffffff
// 24位有符号数的最小值
#define INT24_MIN (-INT24_MAX - 1)

// 1111 xxxx，后4位存储整数值
// 使用这一编码的节点没有相应的content属性，因为编码本身的xxxx四位已经保存了一个介于0和12之间的值，所以它无须content属性
#define ZIP_INT_IMM_MASK 0x0f
// 1111 0001，ZIP_INT_IMM所能表示的最小值为0，但是这里将其加了1，即这里的1表示真实存的是0
#define ZIP_INT_IMM_MIN 0xf1
// 1111 1101，ZIP_INT_IMM所能表示的最大值为12，但是这里将其加了1，即这里的13表示真实存的是12
#define ZIP_INT_IMM_MAX 0xfd

#define ZIP_BIG_PREVLEN 254

// ZIP_STR编码的，开头不会为11，所以与之后，应该都小于1100 0000
#define ZIP_IS_STR(enc) (((enc) & ZIP_STR_MASK) < ZIP_STR_MASK)


// ZIPLIST_INT编码的数值所占的长度，即content的长度
unsigned int zipIntSize(unsigned  char encoding) {
    switch (encoding) {
        case ZIP_INT_8B: return 1;
        case ZIP_INT_16B: return 2;
        case ZIP_INT_24B: return 3;
        case ZIP_INT_32B: return 4;
        case ZIP_INT_64B: return 8;
    }

    // 如果是encoding中直接存储数值，就不需要content了，content长度为0
    if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX)
        return 0;       /* 4bit*/

    panic("Invalid integer encoding 0x%02X", encoding);
    return 0;
}

// 临时记录结构
typedef struct zlentry {
    unsigned int prevrawlensize;    // 当前项prevlen字段的长度
    unsigned int prevrawlen;        // 前一项的长度
    unsigned int lensize;           // 存储当前项长度所需要的长度
    unsigned int len;               // 当前项的长度
    unsigned int heahersize;        // prevrawlensize + lensize
    unsigned char encoding;         // 当前项的编码方式
    unsigned char* p;               // 指向当前项
} zlentry;


// 指向ziplist存储总字节数的字段，位于ziplist开头的32位中
#define ZIPLIST_BYTES(zl)   (*(uint32_t*)(zl))
// 指向ziplist存储列表最后一个元素离列表头偏移的字段，位于ziplist距开头32位-64位中
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 指向ziplist存储列表中的元素个数的字段，位于ziplist距开头64位-80位中，16位，最大存储uint16_t大小个元素
#define ZIPLIST_LENGTH(zl) (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))

// 获取prevlen字段的长度
// 判断ziplist项中prevlen字段的长度，如果prevlen的值小于254(ZIP_BIG_PREVLEN)，只用一个字节存储，所以prevlensize就为1，
//  如果大于等于254，那就使用五个字节存储，其中第一个字节是254，后面四个字节来存储长度值，所以prevlensize为5
#define ZIP_DECODE_PREVLENSIZE(ptr, prevlensize) do { \
    if ((ptr)[0] < ZIP_BIG_PREVLEN) {                 \
        (prevlensize) = 1;                                                  \
    } else {                                          \
        (prevlensize) = 5;                                                  \
    }                               \
} while(0)

// 获取prevlen字段的长度以及prevlen字段的值
// 如果prevlen字段的长度为1，即prevlensize为1，则prevlen的值存储在项的第一个字节
// 如果prevlen字段的长度为5，即prevlensize为5，则prevlen的值存储在项的第二个字节到第五个字节，其中项的第一个字节为254
#define ZIP_DECODE_PREVLEN(ptr, prevlensize, prevlen) do { \
    ZIP_DECODE_PREVLENSIZE(ptr, prevlensize);              \
    if ((prevlensize) == 1) {                              \
        (prevlen) = (ptr)[0];                                                       \
    } else if ((prevlensize) == 5) {                       \
        assert(sizeof((prevlen)) == 4);                    \
        memcpy(&(prevlen), ((char*)(ptr)) + 1, 4);          \
        memrev32ifbe(&prevlen);\
    }\
} while(0)

// 获取encoding字段首字节的值，传入的是ziplist项跳过prevlen字段之后的指针，即开头是encoding字段
// encoding值小于ZIP_STR_MASK的一定是ZIP_STR编码，因为ZIP_INT编码开头两位都是11
#define ZIP_ENTRY_ENCODING(ptr, encoding) do { \
    (encoding) = (ptr[0]);                     \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;\
} while(0)

// 根据encoding字段的值，获取encoding字段的长度(lensize)以及content的长度值(len)
// 传入的是ziplist项跳过prevlen字段之后的指针
#define ZIP_DECODE_LENGTH(ptr, encoding, lensize, len) do { \
    ZIP_ENTRY_ENCODING((ptr), (encoding));                  \
    if ((encoding) < ZIP_STR_MASK) {                        \
        if((encoding) == ZIP_STR_06B) {                     \
            (lensize) = 1;                                  \
            (len) = (ptr)[0] & 0x3f; \
        } else if ((encoding) == ZIP_STR_14B) {             \
            (lensize) = 2;                                  \
            (len) = (((ptr)[0] & 0x3f) << 8) | (ptr)[1];\
        } else if ((encoding) == ZIP_STR_32B) {             \
            (lensize) = 5;                                  \
            (len) = ((ptr)[1] << 24) |                      \
                    ((ptr)[2] << 16) |                      \
                    ((ptr)[3] << 8) |                       \
                    ((ptr)[4]);     \
        } else {                                            \
            panic("Invalid string encoding 0x%02X", (encoding));                                                    \
        }                                         \
    } else {                                                \
        (lensize) = 1;                                      \
        (len) = zipIntSize(encoding); \
    }\
} while(0)

// ziplist最大只能存储uint16_t大小个元素，所以加了个判断
#define ZIPLIST_INCR_LENGTH(zl, incr) { \
    if (ZIPLIST_LENGTH(zl) < UINT16_MAX)\
    ZIPLIST_LENGTH(zl) = intrev16ifbe(intrev16ifbe(ZIPLIST_LENGTH(zl)) + incr);\
}

// ziplist头的大小
#define ZIPLIST_HEADER_SIZE (sizeof(uint32_t)*2 + sizeof(uint16_t))
// ziplist最后一个元素指针
#define ZIPLIST_ENTRY_TAIL(zl) ((zl) + intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)))
// ziplist第一个元素指针
#define ZIPLIST_ENTRY_HEAD(zl) ((zl) + ZIPLIST_HEADER_SIZE)

unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);

unsigned int zipRawEntryLength(unsigned char *ptail);

int zipPrevLenByteDiff(unsigned char *p, size_t len);

unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p);

void zipSaveInteger(unsigned char *p, long long int value, unsigned char encoding);

unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num);




// 创建ziplist
unsigned char* ziplistNew(void) {
    unsigned int bytes = ZIPLIST_HEADER_SIZE +ZIPLIST_END_SIZE;
    unsigned char* zl = zmalloc(bytes);
    // 设置ziplist总字节数字段，位于ziplist开头的32位中
    ZIPLIST_BYTES(zl) = intrev32ifbe(bytes);
    // 设置ziplist存储列表最后一个元素离列表头偏移的字段，位于ziplist距开头32位-64位中
    ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(ZIPLIST_HEADER_SIZE);
    // 设置ziplist存储列表中的元素个数的字段，位于ziplist距开头64位-80位中
    ZIPLIST_LENGTH(zl) = 0;
    // 将压缩列表尾字段设置为ZIP_END，列表尾字段占1个字节
    zl[bytes-1] = ZIP_END;
    return zl;
}

// 扩缩容
unsigned char* ziplistResize(unsigned char* zl, unsigned int len) {
    zl = zrealloc(zl, len);
    ZIPLIST_BYTES(zl) = intrev32ifbe(len);
    zl[len-1] = ZIP_END;
    return zl;
}

// 插入
unsigned char* ziplistInsert(unsigned char* zl, unsigned char* p, unsigned char* s, unsigned int slen) {
    return __ziplistInsert(zl, p, s, slen);
}

// 尝试使用ZIPLIST_INT对元素进行编码，返回0表示编码失败，返回1表示编码成功，同时v返回转换后的数值，encoding返回具体的编码类型
int zipTryEncoding(unsigned char *entry, unsigned int entrylen, long long *v, unsigned char *encoding) {
    long long value;

    // 长度超出ZIPLIST_INT所能表示的最大编码所能表示的大小，即超出了int64所能表示的数值大小，只能使用ZIPLIST_STR编码进行存储
    if (entrylen >= 32 || entrylen == 0) return 0;

    if (string2ll((char *)entry, entrylen, &value)) {
        if(value >= 0 && value <=12) {
            *encoding = ZIP_INT_IMM_MIN + value;
        } else if(value >= INT8_MIN && value <= INT8_MAX) {
            *encoding = ZIP_INT_8B;
        } else if(value >= INT16_MIN && value <= INT16_MAX) {
            *encoding = ZIP_INT_16B;
        } else if(value >= INT24_MIN && value <= INT24_MAX) {
            *encoding = ZIP_INT_24B;
        } else if(value >= INT32_MIN && value <= INT32_MAX) {
            *encoding = ZIP_INT_32B;
        } else {
            *encoding = ZIP_INT_64B;
        }
        *v = value;
        return 1;
    }
    return 0;
}

// prevlensize 5字节的情况，根据前一项的长度len，来设置本项的prelen的值，并返回prevlensize，如果p为NULL，不设置只返回prevlensize
int zipStorePrevEntryLengthLarge(unsigned char* p, unsigned int len) {
    if (p != NULL) {
        p[0] = ZIP_BIG_PREVLEN;
        memcpy(p+1, &len, sizeof(len));
        memrev32ifbe(p+1);
    }
    return 1 + sizeof(len);
}


// 根据p项前一项的长度len，来设置p项的prelen的值，并返回prevlensize，如果p为NULL，不设置只返回prevlensize
unsigned int zipStorePrevEntryLength(unsigned char* p, unsigned int len) {
    if (p == NULL) {    // 不设置p项的prevlen字段的值
        // prevlensize 1字节或5字节
        return (len < ZIP_BIG_PREVLEN) ? 1 : sizeof(len) + 1;
    } else {
        // prevlensize 1字节
        if (len < ZIP_BIG_PREVLEN) {
            p[0] = len;
            return 1;
        } else {    // prevlensize 5字节
            return zipStorePrevEntryLengthLarge(p, len);
        }
    }
}

// 根据p项的编码类型(encoding)，来设置p的encoding字段的值，并返回encoding字段的长度，如果p为NULL，不设置只返回encoding字段的长度
// rawlen是ZIP_STR编码类型的content的长度，即字符串的长度
unsigned int zipStoreEntryEncoding(unsigned char* p, unsigned char encoding, unsigned int rawlen) {
    unsigned char len = 1, buf[5];

    if (ZIP_IS_STR(encoding)) { // str
        if(rawlen < 0x3f) { // ZIP_STR_06B编码即可放得下
            if (!p) return len;
            buf[0] = ZIP_STR_06B | rawlen;
        } else if (rawlen < 0x3fff) {   // ZIP_STR_14B编码即可放得下
            len += 1;
            if (!p) return len;
            buf[0] = ZIP_STR_14B | ((rawlen >> 8) & 0x3f);
            buf[1] = rawlen & 0xff;
        } else { // ZIP_STR_32B编码即可放得下
            len += 4;
            if (!p) return len;
            buf[0] = ZIP_STR_32B;
            buf[1] = ((rawlen >> 24) & 0xff);
            buf[2] = ((rawlen >> 16) & 0xff);
            buf[3] = ((rawlen >> 8) & 0xff);
            buf[4] = rawlen & 0xff;
        }
    } else {    // int
        // ZIP_INT编码的encoding字段都只占一个字节
        if (!p) return len;
        buf[0] = encoding;
    }

    memcpy(p, buf, len);
    return len;
}

// 创建zlentry结构，由参数e返回
void zipEntry(unsigned char* p, zlentry* e) {
    ZIP_DECODE_PREVLEN(p, e->prevrawlensize, e->prevrawlen);
    ZIP_DECODE_LENGTH(p + e->prevrawlensize, e->encoding, e->lensize, e->len);
    e->heahersize = e->prevrawlensize + e->encoding;
    e->p = p;
}

// 在ziplist中p项之前插入元素s
unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    // curlen: 当前ziplist的总长度，reqlen: 当前插入的项的长度
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen;
    unsigned int prevlensize, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789;

    zlentry tail;

    // 获取插入位置前一项的长度，即获取p项前一项的长度
    // 如果p项不为结束标识符，说明是在ziplist中插入，需要获取p项前一项的长度，即存储在p项中开头的prevlen
    if(p[0] != ZIP_END) {
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
    } else {    // 如果p项为结束标识符，在结束符之前插入，就说明要在ziplist结尾插入，需要获取zipkist结尾项的长度
        unsigned char* ptail = ZIPLIST_TAIL_OFFSET(zl);
        if (ptail[0] != ZIP_END) {  // 如果ptail[0] == ZIP_END，表示ziplist此时为空，没有任何项
            prevlen = zipRawEntryLength(ptail);
        }
    }

    // 计算content的长度
    // 元素s是否可以使用ZIPLIST_INT编码
    if (zipTryEncoding(s, slen, &value, &encoding)) {
        // 返回content字段的长度
        reqlen = zipIntSize(encoding);
    } else {    // 元素是使用ZIPLIST_STR编码的，则content字段的长度就是元素s的长度
        reqlen = slen;
    }

    // 计算prevlen字段的长度
    reqlen += zipStorePrevEntryLength(NULL, prevlen);
    // 计算encoding字段的长度
    reqlen += zipStoreEntryEncoding(NULL, encoding, slen);
    // 此时reqlen计算出来当前插入的项的总长度

    // 如果插入不是末尾，就需要确保插入位置的后一项的prevlen能够存储下当前项的总长度
    int forcelarge = 0;
    nextdiff = (p[0] != ZIP_END) ? zipPrevLenByteDiff(p, reqlen) : 0;
    // 如果p项的prevlen够存储当前插入项的总长度，并且还富余4字节
    if (nextdiff == -4 && reqlen < 4) {
        nextdiff = 0;   // 用于后面的判断
        forcelarge = 1; // 原本p的prevlen就是5字节长，表明p项存储prevlen是使用5字节的
    }

    // 保存p项首部到ziplist首部的偏移，因为重新分配内存之后，可能会导致zl的地址发生变化
    offset = p - zl;
    // 插入当前项之后的ziplist大小为，当前ziplist长度+当前项的长度+下一项prevlensize需要扩容的长度
    zl = ziplistResize(zl, curlen+reqlen+nextdiff);
    p = zl + offset;

    // 移动内存，以及更新ziplist存储列表最后一个元素离列表头偏移的字段
    if (p[0] != ZIP_END) {  // 插入位置不是ziplist末尾
        // 将插入位置之后的所有元素，移动到合适的问题，给插入元素腾挪位置，同时预留了p项的prevlen字段扩容的那部分内存
        memmove(p+reqlen, p-nextdiff,curlen-offset-1+nextdiff);

        // 根据插入项的总长度更新p项的prevlen
        if (forcelarge) // 优化，不需要再判断存储reqlen需要多大空间了，直接设置即可
            // p+reqlen是插入新元素之后新的p的位置
            zipStorePrevEntryLengthLarge(p+reqlen, reqlen);
        else
            zipStorePrevEntryLength(p+reqlen, reqlen);

        // 更新ziplist存储列表最后一个元素离列表头偏移的字段，仅仅加上了插入的项的总长度，没有考虑p项prevlen字段可能会扩容的长度
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl) + reqlen));

        // 创建zlentry结构
        zipEntry(p+reqlen, &tail);
        // 如果p不是ziplist最后一项，则计算ziplist最后一项的偏移量，需要加上p项prevlen字段扩容的长度，因为ziplist最后一项的偏移量会包括该长度
        // 如果p是ziplist最后一项，就不需要考虑p项prevlen字段扩容的长度，因为ziplist最后一项的偏移量不包括该长度
        if(p[reqlen+tail.heahersize+tail.len] != ZIP_END) {  // (p + reqlen) + (prevlensize + encodingsize) + (contentsize)，就是p项后一项的位置（p+reqlen是插入新元素之后新的p的位置）
            ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl) + nextdiff));
        }
    } else {    // 插入位置是ziplist结尾，只需更新ziplist最后一项的偏移量即可，不需要移动内存，因为当前插入项本身就是最后一项了
        // 如果插入位置是ziplist结尾，即p就是指向当前ziplist结尾的指针，新插入的项就是从p位置往后存放的，又因为当前插入项本身就是最后一项，所以最后一项的位置就是当前p指针的位置
        ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(p-zl);
    }

    // 如果插入项后一项的p项的prevlensize发生了变化，则p项的后一项的prevlensize也可能发生变化，以此类推到最后一项
    if (nextdiff != 0) {
        // 保存p项首部到ziplist首部的偏移，因为重新分配内存之后，可能会导致zl的地址发生变化
        offset = p - zl;
        zl = __ziplistCascadeUpdate(zl, p+reqlen); // p+reqlen是插入新元素之后新的p的位置
        p = zl + offset;
    }

    // 存储新插入entry
    // p此时就是需要插入的新项的首地址，p+reqlen是插入新元素之后新的p项的位置
    // 设置新项的prevlen字段的值，并将p指针移到下一个字段的位置
    p += zipStorePrevEntryLength(p, prevlen);
    // 设置新项的encoding字段的值，并将p指针移到下一个字段的位置
    p += zipStoreEntryEncoding(p, encoding, slen);
    // 设置新项的content字段的值
    if (ZIP_IS_STR(encoding)) {
        // 将插入的元素s内容copy到新项的content字段
        memcpy(p, s, slen);
    } else {
        zipSaveInteger(p, value, encoding);
    }
    // 增加ziplist存储的元素的个数
    ZIPLIST_INCR_LENGTH(zl, 1);
    return zl;
}

// 根据encoding读取int编码的数值，p为指向int编码项的content的指针
int64_t zipLoadInteger(unsigned char* p, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64, ret = 0;

    if (encoding == ZIP_INT_8B) {   // 存储int8_t类型的数值
        ret = ((int8_t*)p)[0];
    } else if(encoding == ZIP_INT_16B) { // 存储int16_t类型的数值
        memcpy(&i16, p, sizeof(i16));
        memrev16ifbe(&i16);
        ret = i16;
    } else if(encoding == ZIP_INT_24B) { // 存储int24_t类型的数值
        i32 = 0;
        // i32跳过8位 低位 之后存储读取的24位的值
        memcpy(((int8_t*)&i32) + 1, p, sizeof(i32) - sizeof(uint8_t));
        memrev32ifbe(&i32);
        // 因为上面存储是跳过8位低位存储的，所以要右移8位
        ret = i32 >> 8;
    } else if(encoding == ZIP_INT_32B) { // 存储int32_t类型的数值
        memcpy(&i32, p, sizeof(i32));
        memrev16ifbe(&i32);
        ret = i32;
    } else if(encoding == ZIP_INT_64B) { // 存储int64_t类型的数值
        memcpy(&i64, p, sizeof(i64));
        memrev16ifbe(&i64);
        ret = i64;
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        // 记得要减1，存储时候是真实值加一存储的，读取时要减1才能获取真实值
        ret = (encoding & ZIP_INT_IMM_MASK) - 1;
    } else {
        assert(NULL);
    }
    return ret;
}

// 根据encoding的值，将整型value存储在p位置
void zipSaveInteger(unsigned char *p, long long int value, unsigned char encoding) {
    int16_t i16;
    int32_t i32;
    int64_t i64;

    if (encoding == ZIP_INT_8B) {   // 存储int8_t类型的数值
        ((int8_t*)p)[0] = (int8_t)value;
    } else if(encoding == ZIP_INT_16B) { // 存储int16_t类型的数值
        i16 = value;
        memcpy(p, &i16, sizeof(i16));
        // 大小端转换
        memrev16ifbe(p);
    } else if(encoding == ZIP_INT_24B) { // 存储int24_t类型的数值
        i32 = value << 8;
        memrev32ifbe(&i32);
        // copy 24字节，((uint8_t*)&i32) + 1是为了跳过前8位，上面将value左移了8位，所以真正数据存储在8位之后，所以这里要跳过前8位
        memcpy(p, ((uint8_t*)&i32) + 1, sizeof(i32) - sizeof(uint8_t));
        // 大小端转换
        memrev16ifbe(p);
    } else if(encoding == ZIP_INT_32B) { // 存储int32_t类型的数值
        i32 = value;
        memcpy(p, &i32, sizeof(i32));
        // 大小端转换
        memrev32ifbe(p);
    } else if(encoding == ZIP_INT_64B) { // 存储int64_t类型的数值
        i64 = value;
        memcpy(p, &i64, sizeof(i64));
        // 大小端转换
        memrev64ifbe(p);
    } else if (encoding >= ZIP_INT_IMM_MIN && encoding <= ZIP_INT_IMM_MAX) {
        // 什么都不用做，因为该情况下，数值存储在encoding中了，不需要content
    } else {
        assert(NULL);
    }
}

// 如果插入项后一项的p项的prevlensize发生了变化，则p项的后一项的prevlensize也可能发生变化，以此类推到最后一项，这个函数就是处理这种可能会发生连锁更新的情况
// 或者删除项的后一项的prevlensize发生了变化，则其的后一项的prevlensize也可能发生变化，以此类推到最后一项

// p项的长度发生了变化，可能会导致p项的后一项的长度发生变化，依次类推，即发生了连锁更新
unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), rawlen, rawlensize;
    size_t offset, noffset, extra;
    unsigned char* np;
    zlentry cur, next;

    // 从p项开始遍历ziplist，直到结尾
    while (p[0] != ZIP_END) {
        // 存储p的信息
        zipEntry(p, &cur);
        // p项的总长度
        rawlen = cur.heahersize + cur.len;
        // 存储p项的长度所需要的字段大小
        rawlensize = zipStorePrevEntryLength(NULL, rawlen);

        // p项的下一项是结尾
        if (p[rawlen] == ZIP_END) break;

        // 存储p项的下一项的信息
        zipEntry(p+rawlen, &next);

        // 下一项的prevlen的值，等于当前项的总长度，即当前项的总长度没有变化，就说明不需要更新后续的项，没有连锁更新，直接退出
        if (next.prevrawlen == rawlen) break;

        // 下一项prevlen不够存储当前项的总长度，需要连锁更新后面的项
        if (next.prevrawlensize < rawlensize) {
            // 保存p项首部到ziplist首部的偏移，因为重新分配内存之后，可能会导致zl的地址发生变化
            offset = p - zl;
            extra = rawlensize - next.prevrawlensize;   // 下一项prevlen需要额外的空间来存储当前项的总长度
            zl = ziplistResize(zl, curlen + extra); // 扩容ziplist
            p = zl + offset;    // 计算新的p项的地址

            np = p + rawlen;    // p项下一项的首地址
            noffset = np - zl;  // p项下一项的偏移量

            // 如果p项的下一项不是ziplist最后一项，就需要更新zl尾指针，如果是最后一项就不需要更新尾指针，因为扩容的位置是在尾指针之后了
            if ((zl+ intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl))) != np) {
                // 因为zl整体扩容了extra个字节，同时扩容的位置不是在最后一个元素，则原先的尾指针也会向后移动extra个字节
                ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + extra);
            }

            // 将 从下一项除去prevlen字段开始到ziplist结尾的内容 全部移动到新的位置(np+rawlensize之后，即下一项prevlen字段之后)，同时预留出下一项prevlen的位置(rawlensize)
            memmove(np+rawlensize, np+next.prevrawlensize, curlen-noffset-next.prevrawlensize-1);
            // 设置下一项prevlen字段的值
            zipStorePrevEntryLength(np, rawlen);

            // 当前遍历项的指针往后移动一项，即当前的下一项变成了下一次遍历的当前项，继续循环
            p += rawlen;
            // 维护循环变量，当前ziplist的总长度，下一次循环也会用到这个值
            curlen += extra;
        } else {    // 下一项prevlen够存储当前项的总长度，但是当前项的总长度发生了变化，所以只需要修改下一项prevlen的值即可，不需要连锁更新
            if (next.prevrawlensize > rawlensize) { // 下一项的prevlen为5字节长度，因为prevlen只有1字节和5字节
                zipStorePrevEntryLengthLarge(p+rawlen, rawlen);
            } else {    // 下一项的prevlen可能为5字节长度，也可能为1字节长度
                zipStorePrevEntryLength(p+rawlen, rawlen);
            }

            break;
        }
    }

    return zl;
}

// 计算存储传入的len所需要的prevlensize和p项的prevlensize的差值
// 如果为正，说明p的prevlen不够存储len，如果为负或者0，说明p的prevlen是够存储len的
// 因为prevlensize的取值只有1和5，所以结果只能为4、-4或者0，不够存的情况只有4
int zipPrevLenByteDiff(unsigned char *p, size_t len) {
    unsigned int prevlensize;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    return zipStorePrevEntryLength(NULL, len) - prevlensize;
}

// 返回p项整体的长度 = prevlensize+encodingsize(lensize)+contentlen(len)
unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);
    ZIP_DECODE_LENGTH(p+prevlensize, encoding, lensize, len);
    return prevlensize + lensize + len;
}

// 删除一项
unsigned char* ziplistDelete(unsigned char* zl, unsigned char** p) {
    size_t offset = *p - zl;
    zl = __ziplistDelete(zl, *p, 1);

    // 使p指向删除之后的原位置，如果删除的是最后一项，则p为ZIP_END
    *p = zl + offset;
    return zl;
}

// 删除num个项，从p项开始
unsigned char *__ziplistDelete(unsigned char *zl, unsigned char *p, unsigned int num) {
    unsigned int i, totlen, deleted = 0;
    size_t offset;
    int nextdiff = 0;
    zlentry first, tail;

    zipEntry(p, &first);
    for(i = 0; p[0] != ZIP_END && i < num; i++) {
        p += zipRawEntryLength(p);  // 往后移动p指针，移动num个项，或者是移动到ziplist的结尾
        deleted++;  // 记录实际删除了几项，因为可能没到num项，就已经删到了ziplist的结尾
    }

    // p指针此时是指向num个项之后的位置
    totlen = p - first.p;   // 需要删除的总长度，就是跳过num项之后p的位置，减去没跳过之前的初始位置
    if (totlen > 0) {
        if (p[0] != ZIP_END) {  // 删除项后一项不是结尾，即没有删除到ziplist结尾
            // 计算存储传入的prevrawlen所需要的prevlensize和p项的prevlensize的差值（p指针此时是指向num个项之后的位置，即删除项后一个项的位置）
            // 即计算存储 删除的第一项的前一项的长度 所需要的prevlensize 和 删除项后一项元素的prevnlensize的差值
            nextdiff = zipPrevLenByteDiff(p, first.prevrawlen);

            // 预留出 删除项 后一项元素的prevnlen 需要扩容的空间
            p -= nextdiff;
            // 将删除项前一项的总长度存储到删除后一项的prevlen中
            zipStorePrevEntryLength(p, first.prevrawlen);

            // 更新ziplist的尾指针
            ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) - totlen);
            zipEntry(p, &tail);
            // 删除项后一项不是最后一项，ziplist的尾指针需要修，加上删除项后一项prevlen扩容的空间
            if (p[tail.heahersize + tail.len] != ZIP_END) {
                ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe(intrev32ifbe(ZIPLIST_TAIL_OFFSET(zl)) + nextdiff);
            }

            // 将从删除项后一项开始直到ziplist结尾的所有内容，移动到被删除的第一项的位置
            memmove(first.p, p, intrev32ifbe(ZIPLIST_BYTES(zl)) - (p-zl) - 1);
        } else {    // 删除的元素包括最后一项，则尾指针应该指向删除项前一项的首地址
            // 被删除的第一项的偏移量 - 被删除第一项的前一项的总长度，就是删除前一项的首地址
            ZIPLIST_TAIL_OFFSET(zl) = intrev32ifbe((first.p - zl) - first.prevrawlen);
        }

        // resize ziplist
        offset = first.p - zl;
        zl = ziplistResize(zl, intrev32ifbe(ZIPLIST_BYTES(zl)) - totlen + nextdiff);
        // 更新ziplist中元素的数量
        ZIPLIST_INCR_LENGTH(zl, -deleted);
        p = zl + offset;

        // 如果删除项的后一项的prevlensize发生了改变，可能会引起连锁更新
        if (nextdiff != 0) {
            // 连锁更新
            zl = __ziplistCascadeUpdate(zl , p);
        }
    }
    return zl;
}

// 返回ziplist第index项的指针（index为负值，从后往前，为正值，从前往后）
unsigned char* ziplistIndex(unsigned char* zl, int index) {
    unsigned char* p;
    unsigned int prevlensize, prevlen = 0;

    if (index < 0) {
        index = (-index) - 1;
        // 从ziplist的尾部开始往前遍历
        p = ZIPLIST_ENTRY_TAIL(zl);
        if (p[0] != ZIP_END) {
            ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
            while (prevlen > 0 && index--) {
                p -= prevlen;
                ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
            }
        }
    } else {
        // 从ziplist的头部开始往后遍历
        p = ZIPLIST_ENTRY_HEAD(zl);
        while (p[0] != ZIP_END && index--) {
            p += zipRawEntryLength(p);
        }
    }
    return (p[0] == ZIP_END || index > 0) ? NULL : p;
}


// 删除多项，第index项开始，持续总共num项
unsigned char* ziplistDeleteRange(unsigned char* zl, int index, unsigned int num) {
    unsigned char* p = ziplistIndex(zl, index);
    return (p == NULL) ? zl : __ziplistDelete(zl, p, num);
}

// 查询ziplist中从p项开始，存储的content值等于vstr的项，并返回
// 如果第一次查询没有查到，则跳过skip项之后继续查询，如果这次查询又每查到，则又跳过skip项，即每次查询中间间隔skip项
unsigned char* ziplistFind(unsigned char* p, unsigned char* vstr, unsigned int vlen, unsigned int skip) {
    int skipcnt = 0;
    unsigned char vencoding = 0;
    long long vll = 0;

    while (p[0] != ZIP_END) {
        unsigned int prevlensize, encoding, lensize, len;
        unsigned char* q;

        ZIP_DECODE_PREVLENSIZE(p, prevlensize);
        ZIP_DECODE_LENGTH(p + prevlensize, encoding, lensize, len);
        // 将p移动到当前遍历项的content处，q就指向该处
        q = p + prevlensize + lensize;

        if (skipcnt == 0) {
            // 当前项存储的是str
            if (ZIP_IS_STR(encoding)) {
                // 比较传入的字符串和当前项存储的字符串是否相同
                if (len == vlen && memcmp(q, vstr, vlen) == 0) {
                    return p;
                }
            } else {    // 当前项存储的是整型
                if (vencoding == 0) {
                    // 尝试将vstr转成整型并进行ziplist编码，如果编码成功的话，vencoding存储vstr的编码值
                    // 编码成功的话，vencoding不为0，后续就不需要再进行编码，不会走该逻辑
                    // 编码失败的话，表明vstr不是数值，vencoding = UCHAR_MAX，下次再遇到整型编码的项，直接跳过即可，不用再进行比较了
                    if (!zipTryEncoding(vstr, vlen, &vll, &vencoding)) {
                        vencoding = UCHAR_MAX;
                    }
                    assert(vencoding);
                }

                // 上一步编码成功，表明vstr是数值，取出当前遍历项存储的数值，进行比较
                if (vencoding != UCHAR_MAX) {
                    long long ll = zipLoadInteger(q, encoding);
                    if (ll == vll) {
                        return p;
                    }
                }
            }

            // reset skip
            skipcnt = skip;
        } else {
            // 跳过当前遍历的项
            skipcnt--;
        }

        // p指向当前遍历项的content处，len是content的长度
        // 将p移动到下一项开始处
        p = q + len;
    }

    return NULL;
}

// 比较给定的元素是否和p项中存储的元素相同
unsigned int ziplistCompare(unsigned char* p, unsigned char* sstr, unsigned int slen) {
    zlentry entry;
    unsigned char sencoding;
    long long zval, sval;

    if (p[0] == ZIP_END) return 0;

    zipEntry(p, &entry);

    if (ZIP_IS_STR(entry.encoding)) {   // ZIP_STR编码
        if (entry.len == slen) {
            return memcmp(p+entry.heahersize, sstr, slen) == 0;
        } else {
            return 0;
        }
    } else {    // ZIP_INT编码
        if (zipTryEncoding(sstr, slen, &sval, &sencoding)) {
            zval = zipLoadInteger(p+entry.heahersize, entry.encoding);
            return zval == sval;
        }
    }
    return 0;
}

// 将元素插入到ziplist的头或尾
unsigned char* ziplistPush(unsigned char* zl, unsigned char* s, unsigned int slen, int where) {
    unsigned char *p;
    p = (where == ZIPLIST_HEAD) ? ZIPLIST_ENTRY_HEAD(zl) : ZIPLIST_ENTRY_TAIL(zl);
    return __ziplistInsert(zl, p, s, slen);
}


// 获取p项中存储的值，获取成功返回1，获取失败返回0
// 如果是ZIP_STR编码返回值以sstr和slen返回
// 如果是ZIP_INT编码返回值以sval返回
unsigned int ziplistGet(unsigned char* p, unsigned char** sstr, unsigned int* slen, long long *sval) {
    zlentry entry;
    if(p == NULL || p[0] == ZIP_END) return 0;
    if (sstr) *sstr = NULL;

    zipEntry(p, &entry);
    if (ZIP_IS_STR(entry.encoding)) {
        if (sstr) {
            *slen = entry.len;
            *sstr = p + entry.heahersize;
        }
    } else {
        if (sval) {
            *sval = zipLoadInteger(p+entry.heahersize, entry.encoding);
        }
    }
    return 1;
}

// 获取zl中p项的下一项
unsigned char* ziplistNext(unsigned char* zl, unsigned char* p) {
    ((void )zl);

    if (p[0] == ZIP_END) {
        return NULL;
    }

    p += zipRawEntryLength(p);
    if (p[0] == ZIP_END) {
        return NULL;
    }

    return p;
}

// 获取zl中p项的前一项
unsigned char* ziplistPrev(unsigned char* zl, unsigned char* p) {
    unsigned int prevlensize, prevlen = 0;

    if (p[0] == ZIP_END) {  // 结尾的前一项是ziplist的最后一项
        p = ZIPLIST_ENTRY_TAIL(zl);
        // 如果相等，表明ziplist为空
        return (p[0] == ZIP_END) ? NULL : p;
    } else if (p == ZIPLIST_ENTRY_HEAD(zl)) {   // 如果p是ziplist第一项，则没有前一项
        return NULL;
    } else {
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
        assert(prevlen > 0);
        return p-prevlen;
    }
}

// 获取ziplist存储的元素个数
// 因为ziplist头中存储元素个数的字段只有16位，4个字节
// 如果ziplist中的元素个数超出了uint16所能表示的范围，则ziplist头中的字段就不对了，需要调用该函数获取真正的元素个数
unsigned int ziplistLen(unsigned char* zl) {
    unsigned int len = 0;
    // 如果ziplist头中的元素个数小于uint16所能表示的最大值，说明头中存储的是正确的，还能放得下，就放
    if (intrev16ifbe(ZIPLIST_LENGTH(zl)) < UINT16_MAX) {
        len = intrev16ifbe(ZIPLIST_LENGTH(zl));
    } else {    // 如果ziplist头中的元素个数大于等于uint16所能表示的最大值，说明已经放不下了，头中存储的是错误的，就需要遍历ziplist的项去统计总共有多少个项
        unsigned char* p = zl + ZIPLIST_HEADER_SIZE;
        while (*p != ZIP_END) {
            p += zipRawEntryLength(p);
            len++;
        }

        // 如果统计出来的项个数小于uint16所能表示的最大值，则重新将该值写回ziplist头中相应的字段
        // 删除还未完成的情况（还未到更新元素数量的逻辑，此时查询头中的元素个数还是删除前的）
        if (len < UINT16_MAX) ZIPLIST_LENGTH(zl) = intrev16ifbe(len);
    }
    return len;
}

// 合并两个ziplist
unsigned char* ziplistMerge(unsigned char** first, unsigned char** second) {
    if (first == NULL || *first == NULL || second == NULL || *second == NULL)
        return NULL;

    if (*first == *second)
        return NULL;

    // 第一个ziplist总字节数
    size_t first_bytes = intrev32ifbe(ZIPLIST_BYTES(*first));
    // 第一个ziplist总元素个数
    size_t first_len = intrev16ifbe(ZIPLIST_LENGTH(*first));

    // 第二个ziplist总字节数
    size_t second_bytes = intrev32ifbe(ZIPLIST_BYTES(*second));
    // 第二个ziplist总字节数
    size_t second_len = intrev16ifbe(ZIPLIST_LENGTH(*second));

    int append;
    unsigned char* source, *target;
    size_t target_bytes, source_bytes;

    if(first_len >= second_len) {   // second追加到first后面
        target = *first;
        target_bytes = first_bytes;
        source = *second;
        source_bytes = second_bytes;
        append = 1;
    } else {        // first拼接到second前面
        target = *second;
        target_bytes = second_bytes;
        source = *first;
        source_bytes = first_bytes;
        append = 0;
    }

    // 计算最终的zl的总长度
    size_t zlbytes = first_bytes + second_bytes - ZIPLIST_HEADER_SIZE - ZIPLIST_END_SIZE;
    // 计算最终的zl的总元素数
    size_t zllength = first_len + second_len;

    // 如果最终zl的总元素个数超过了uint16所能表示的值，就设置为uint16的最大值
    zllength = zllength < UINT16_MAX ? zllength : UINT16_MAX;


    size_t first_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*first));
    size_t second_offset = intrev32ifbe(ZIPLIST_TAIL_OFFSET(*second));


    // 扩容选定的拼接目标，然后将源zl拼接上去
    target = realloc(target, zlbytes);
    if (append) {
        memcpy(target + target_bytes - ZIPLIST_END_SIZE, source + ZIPLIST_HEADER_SIZE, source_bytes - ZIPLIST_HEADER_SIZE);
    } else {
        // 将second zl中的元素移动到合适位置(距离合并后zl开头之后first zl size的位置)，从而在合并后的zl前面给first zl预留空间
        memmove(target + source_bytes - ZIPLIST_END_SIZE, target + ZIPLIST_HEADER_SIZE, target_bytes - ZIPLIST_HEADER_SIZE);
        memcpy(target, source, source_bytes - ZIPLIST_END_SIZE);
    }

    ZIPLIST_BYTES(target) = intrev32ifbe(zlbytes);
    ZIPLIST_LENGTH(target) = intrev16ifbe(zllength);

    // 更新合并后的zl尾指针
    // (first zl - END_SIZE) + (second zl的尾指针 - HEADER_SIZE)
    ZIPLIST_TAIL_OFFSET(target) = intrev32ifbe((first_bytes - ZIPLIST_END_SIZE) + (second_offset - ZIPLIST_HEADER_SIZE));

    // 拼接之前second zl第一项的prevlen是0，因为没有前一项。
    // 但是拼接完之后，second zl第一项的prevlen 是 first zl最后一项的长度，所以second zl的prevlensize肯定会发生变化
    // 所以传入拼接后的zl，以及拼接之后的second zl第一项的前一项，即first zl的最后一项，因为是first zl最后一项导致了后面的项发生了连锁更新
    target = __ziplistCascadeUpdate(target, target+first_offset);

    if (append) {
        zfree(*second);
        *second = NULL;
        *first = target;
    } else {
        zfree(*first);
        *first = NULL;
        *second = target;
    }
    return target;
}

// 获取ziplist的长度
size_t ziplistBlobLen(unsigned char* zl) {
    return intrev32ifbe(ZIPLIST_BYTES(zl));
}