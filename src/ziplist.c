//
// Created by xyzjiao on 9/6/21.
//

#include <stdint.h>
#include <string.h>
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
#define ZIP_INT_IMM_MAX 0xf

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

unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);

unsigned int zipRawEntryLength(unsigned char *ptail);

int zipPrevLenByteDiff(unsigned char *p, size_t len);

unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p);

void zipSaveInteger(unsigned char *p, long long int value, unsigned char encoding);

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
        // // 保存p项首部到ziplist首部的偏移，因为重新分配内存之后，可能会导致zl的地址发生变化
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

unsigned char *__ziplistCascadeUpdate(unsigned char *zl, unsigned char *p) {
    return NULL;
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

