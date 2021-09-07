//
// Created by xyzjiao on 9/6/21.
//

#include <stdint.h>
#include <string.h>
#include "redisassert.h"
#include "endiarconv.h"
#include "ziplist.h"
#include "zmalloc.h"

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
// 1111 0001，最小值1
#define ZIP_INT_IMM_MIN 0xf1
// 1111 1101，最大值13
#define ZIP_INT_IMM_MAX 0xfd

#define ZIP_BIG_PREVLEN 254


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
    unsigned char encoding;         // 编码方式
    unsigned char* p;               //
} zlentry;


// 指向ziplist存储总字节数的字段，位于ziplist开头的32位中
#define ZIPLIST_BYTES(zl)   (*(uint32_t*)(zl))
// 指向ziplist存储列表最后一个元素离列表头偏移的字段，位于ziplist距开头32位-64位中
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
// 指向ziplist存储列表中的元素个数的字段，位于ziplist距开头64位-80位中
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

// 获取encoding字段的值，传入的是ziplist项跳过prevlen字段之后的指针，即开头是encoding字段
// encoding值小于ZIP_STR_MASK的一定是ZIP_STR编码，因为ZIP_INT编码开头两位都是11
#define ZIP_ENTRY_ENCODING(ptr, encoding) do { \
    (encoding) = (ptr[0]);                     \
    if ((encoding) < ZIP_STR_MASK) (encoding) &= ZIP_STR_MASK;\
} while(0)

// 获取encoding字段的值，以及len字段的长度以及len字段的值
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

unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen);

unsigned int zipRawEntryLength(unsigned char *ptail);

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

// 在ziplist中p项之后插入s
unsigned char *__ziplistInsert(unsigned char *zl, unsigned char *p, unsigned char *s, unsigned int slen) {
    // 当前ziplist的总长度
    size_t curlen = intrev32ifbe(ZIPLIST_BYTES(zl)), reqlen;
    unsigned int prevlensize, prevlen = 0;
    size_t offset;
    int nextdiff = 0;
    unsigned char encoding = 0;
    long long value = 123456789;

    zlentry tail;

    // 如果p项不为结束标识符，说明是在ziplist中插入，需要获取p项的长度
    if(p[0] != ZIP_END) {
        ZIP_DECODE_PREVLEN(p, prevlensize, prevlen);
    } else {    // 如果p项为结束标识符，说明要在ziplist结尾插入，需要获取zipkist结尾项的长度
        unsigned char* ptail = ZIPLIST_TAIL_OFFSET(zl);
        if (ptail[0] != ZIP_END) {  // 如果ptail[0] == ZIP_END，表示ziplist此时为空，没有任何项
            prevlen = zipRawEntryLength(ptail);

        }
    }

    return zl;
}

// 返回p项整体的长度
unsigned int zipRawEntryLength(unsigned char *p) {
    unsigned int prevlensize, encoding, lensize, len;
    ZIP_DECODE_PREVLENSIZE(p, prevlensize);

    return 0;
}

