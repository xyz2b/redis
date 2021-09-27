//
// Created by xyzjiao on 9/15/21.
//

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "listpack.h"
#include "listpack_malloc.h"

// listpack头 = 4字节总字节数 + 2字节listpack元素数量
#define LP_HDR_SIZE 6

// listpack结束标记
#define LP_EOF 0xFF

#define LP_MAX_INT_ENCDOING_LEN 9
#define LP_MAX_BACKLEN_SIZE 5

/**
 * 对于整数类型来说，列表项编码类型字段，不仅保存了编码类型，还保存了实际的整数值，整数类型不需要data字段
 * 对于字符串类型来说，列表项编码类型字段，不仅保存了编码类型，还保存了字符串的长度值，data字段保存了实际的字符串
 * */
#define LP_ENCODING_INT 0
#define LP_ENCODING_STRING 1

// listpack编码类型
/**
 * 首先，对于整数编码来说，当listpack元素的编码类型为LP_ENCODING_7BIT_UINT时，表示元素的实际数据是一个7bit的无符号整数(整数编码类型名称中BIT前面的数字，表示的是整数的长度)。
 * 又因为LP_ENCODING_7BIT_UINT本身的宏定义值为0，所以编码类型的值也相应为0，占1bit。
 * 此时，编码类型和元素实际数据共有1个字节，这个字节的最高位为0，表示编码类型，后续的7位用来存储7bit的无符号整数。
 * */
#define LP_ENCODING_7BIT_UINT 0
// 1000 0000
#define LP_ENCODING_7BIT_UINT_MASK 0x80
#define LP_ENCODING_IS_7BIT_UINT(byte) (((byte) & LP_ENCODING_7BIT_UINT_MASK) == LP_ENCODING_7BIT_UINT)

/**
 * 而当编码类型为LP_ENCODING_13BIT_INT时，这表示元素的实际数据是13bit的整数。
 * 同时，因为LP_ENCODING_13BIT_INT的宏定义值为0xC0，转换为二进制值是1100 0000，所以，这个二进制值中的后5位和后续的1个字节，共13位，会用来保存13bit的整数。
 * 而该二进制值中的前3位110，则用来表示当前的编码类型。
 * */
#define LP_ENCODING_13BIT_INT 0xC0
// 1110 0000
#define LP_ENCODING_13BIT_INT_MASK 0xE0
#define LP_ENCODING_IS_13BIT_INT(byte) (((byte) & LP_ENCODING_13BIT_INT_MASK) == LP_ENCODING_13BIT_INT)

// LP_ENCODING_16BIT_INT、LP_ENCODING_24BIT_INT 、LP_ENCODING_32BIT_INT、LP_ENCODING_64BIT_INT分别是用2字节，3字节，4字节和8字节来保存整数数据。
// 同时，它们的编码类型本身占1字节。编码类型的值分别是它们的宏定义值。
#define LP_ENCODING_16BIT_INT 0xF1
#define LP_ENCODING_16BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_16BIT_INT(byte) (((byte)&LP_ENCODING_16BIT_INT_MASK)==LP_ENCODING_16BIT_INT)

#define LP_ENCODING_24BIT_INT 0xF2
#define LP_ENCODING_24BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_24BIT_INT(byte) (((byte)&LP_ENCODING_24BIT_INT_MASK)==LP_ENCODING_24BIT_INT)

#define LP_ENCODING_32BIT_INT 0xF3
#define LP_ENCODING_32BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_32BIT_INT(byte) (((byte)&LP_ENCODING_32BIT_INT_MASK)==LP_ENCODING_32BIT_INT)

#define LP_ENCODING_64BIT_INT 0xF4
#define LP_ENCODING_64BIT_INT_MASK 0xFF
#define LP_ENCODING_IS_64BIT_INT(byte) (((byte)&LP_ENCODING_64BIT_INT_MASK)==LP_ENCODING_64BIT_INT)


/**
 * 对于字符编码来说，一共有三种类型，分别是LP_ENCODING_6BIT_STR、LP_ENCODING_12BIT_STR、LP_ENCODING_32BIT_STR。字符串编码类型名称中BIT前的数字，表示的是字符串长度所占的bit数。
 * 比如当编码类型为LP_ENCODING_6BIT_STR时，该类型的宏定义值是0x80，对应的二进制值为1000 0000，这其中的前2位是用来标识编码类型本身，而后6位保存的是字符串长度。
 * 然后，列表项中的数据部分(data)保存了实际的字符串。
 * */
#define LP_ENCODING_6BIT_STR 0x80
#define LP_ENCODING_6BIT_STR_MASK 0xC0
#define LP_ENCODING_IS_6BIT_STR(byte) (((byte)&LP_ENCODING_6BIT_STR_MASK)==LP_ENCODING_6BIT_STR)

#define LP_ENCODING_12BIT_STR 0xE0
#define LP_ENCODING_12BIT_STR_MASK 0xF0
#define LP_ENCODING_IS_12BIT_STR(byte) (((byte)&LP_ENCODING_12BIT_STR_MASK)==LP_ENCODING_12BIT_STR)

#define LP_ENCODING_32BIT_STR 0xF0
#define LP_ENCODING_32BIT_STR_MASK 0xFF
#define LP_ENCODING_IS_32BIT_STR(byte) (((byte)&LP_ENCODING_32BIT_STR_MASK)==LP_ENCODING_32BIT_STR)

#define LP_ENCODING_6BIT_STR_LEN(p) ((p)[0] & 0x3F)
// LP_ENCODING_12BIT_STR，标识字符串长度是占12位，高4位在第一个字节的低4位(高4位是标识编码类型)，低8位在下一个字节
#define LP_ENCODING_12BIT_STR_LEN(p) ((((p)[0] & 0xF) << 8) | (p)[1])
// LP_ENCODING_32BIT_STR_LEN，标识字符串长度是占32位，低8位在第二个字节，第三个字节，第四个字节，高8位在第五个字节
#define LP_ENCODING_32BIT_STR_LEN(p) (((uint32_t)(p)[1] << 0) | \
                                        ((uint32_t)(p)[2] << 8) | \
                                        ((uint32_t)(p)[3] << 16) | \
                                        ((uint32_t)(p)[4] << 24))

#define LP_HDR_NUMELE_UNKNOWN UINT16_MAX

#define lpSetTotalBytes(p, v) do { \
    (p)[0] = (v) & 0xff;           \
    (p)[1] = ((v)>>8) & 0xff;      \
    (p)[2] = ((v)>>16) & 0xff;     \
    (p)[3] = ((v)>>24) & 0xff;\
                                   \
} while(0)

// 获取lp的总字节数
#define lpGetTotalBytes(p) (((uint32_t)(p)[0] << 0) | \
                                ((uint32_t)(p)[1] << 8) | \
                                 ((uint32_t)(p)[2] << 16) | \
                                 ((uint32_t)(p)[3] << 24))


#define lpGetNumElements(p) (((uint32_t)(p)[4] << 0) | \
                            ((uint32_t)(p)[5] << 8))

#define lpSetNumElements(p, v) do { \
    (p)[4] = (v) & 0xff;         \
    (p)[5] = ((v)>>8) & 0xff;\
} while(0)

unsigned char* lpNew(void) {
    // +1 是一字节的结尾标识
    unsigned char* lp = lp_malloc(LP_HDR_SIZE + 1);
    if (lp == NULL) return NULL;
    lpSetTotalBytes(lp, LP_HDR_SIZE+1);
    lpSetNumElements(lp, 0);
    lp[LP_HDR_SIZE] = LP_EOF;
    return lp;
}

void lpFree(unsigned char* lp) {
    lp_free(lp);
}

// 将string 转成int64整形，转换成功返回1，转换后的值由value返回
int lpStringToInt64(const char* s, size_t slen, long long* value) {
    const char* p = s;
    size_t plen = 0;    // 目前已解析的长度
    int negative = 0;   // 标识解析出来的数值正负
    uint64_t v;

    // 传入的长度为0，直接返回转换失败
    if (plen == slen) {
        return 0;
    }

    // string就是0字符
    if (slen == 1 && p[0] == '0') {
        if (value != NULL) *value = 0;
        return 1;
    }

    // 负值
    if (p[0] == '-') {
        negative = 1;
        p++; plen++;

        // 如果字符串仅仅是'-'，转换成数值失败
        if (plen == slen)
            return 0;
    }

    // 第一个字符是介于字符'1'到字符'9'之间的，第一个字符是最高位
    if (p[0] >= '1' && p[0] <='9') {
        v = p[0] - '0'; // 获取数值
        p++; plen++;
    } else { // 第一个字符就不是数字字符，转换失败
        return 0;
    }

    // 转换第一个字符后面的字符，判断后面的字符是否是数字字符('0'-'9')
    while (plen < slen && p[0] >= '0' && p[0] <= '9') {
        if(v > (UINT64_MAX / 10))   // 溢出，因为后v要乘以10，所以这里要除以10
            return 0;

        v *= 10;

        if (v > (UINT64_MAX - (p[0] - '0')))    // 溢出，因为后面v要加上(p[0] - '0')，所以这里需要减掉
            return 0;

        v += p[0] - '0';
        p++; plen++;
    }

    // 不是所有字符都是数值
    if(plen < slen)
        return 0;

    if (negative) {
        // 有符号整数，比如char，一个字节，所能表示的值为-128 -> 127，所以对( (unsigned char)(-(-128+1)) + 1 )=128，就是负数相反的正数，如果负数去除负号之后的值大于这个值，就是负值溢出了
        // 这里强转的目的是因为有符号数放不下这么大的正值，所以要转成无符号数来存放
        if (v > ((uint64_t)(-(INT64_MIN+1)) + 1))   // 溢出
            return 0;
        if (value != NULL) *value = -v;
    } else {
        if (v > INT64_MAX)  // 溢出
            return 0;
        if (value != NULL) *value = v;
    }

    return 1;
}

// 判断ele能够编码成什么类型，能够编码从整型，就返回LP_ENCODING_INT，不然编码成字符型，返回LP_ENCODING_STRING
// 参数intenc表示整型编码的结果（字符型没有该结果）。enclen是编码+元素值的总长度（整型就是编码的长度，因为编码包含了元素值，对于字符型就是编码的长度+元素值的长度）
int lpEncodeGetType(unsigned char* ele, uint32_t size, unsigned char* intenc, uint64_t* enclen) {
    int64_t v;
    if (lpStringToInt64((const char*)ele, size, &v)) {
        if (v >= 0 && v <= 127) {   // 7bit所能表示的无符号数的范围
            intenc[0] = v;
            *enclen = 1;
        } else if (v >= -4096 && v <= 4095) {   // 13bit所能表示的有符号数的范围
            if (v < 0) v = ((int64_t)1 << 13) + v;  // 让高位为1，表示负值
            intenc[0] = (v >> 8) | LP_ENCODING_13BIT_INT;
            intenc[1] = v & 0xFF;
            *enclen = 2;
        } else if (v >= -32768 && v <= 32767) {   // 16bit所能表示的有符号数的范围
            if (v < 0) v = ((int64_t)1 << 16) + v;  // 让高位为1，表示负值
            intenc[0] = LP_ENCODING_16BIT_INT;
            intenc[1] = v & 0xFF;
            intenc[2] = v >> 8;
            *enclen = 3;
        } else if (v >= -8388608 && v <= 8388607) {   // 24bit所能表示的有符号数的范围
            if (v < 0) v = ((int64_t)1 << 24) + v;  // 让高位为1，表示负值
            intenc[0] = LP_ENCODING_24BIT_INT;
            intenc[1] = v & 0xFF;
            intenc[2] = (v >> 8) & 0xFF;
            intenc[3] = v >> 16;
            *enclen = 4;
        } else if (v >= -2147483648 && v <= 2147483647) {   // 32bit所能表示的有符号数的范围
            if (v < 0) v = ((int64_t)1 << 32) + v;  // 让高位为1，表示负值
            intenc[0] = LP_ENCODING_32BIT_INT;
            intenc[1] = v & 0xFF;
            intenc[2] = (v >> 8) & 0xFF;
            intenc[3] = (v >> 16) & 0xFF;
            intenc[4] = v >> 24;
            *enclen = 5;
        } else { // 64bit所能表示的有符号数的范围
            uint64_t uv = v;
            intenc[0] = LP_ENCODING_64BIT_INT;
            intenc[1] = uv & 0xFF;
            intenc[2] = (uv >> 8) & 0xFF;
            intenc[3] = (uv >> 16) & 0xFF;
            intenc[4] = (uv >> 24) & 0xFF;
            intenc[5] = (uv >> 32) & 0xFF;
            intenc[6] = (uv >> 40) & 0xFF;
            intenc[7] = (uv >> 48) & 0xFF;
            intenc[8] = uv >> 56;
            *enclen = 9;
        }
        return LP_ENCODING_INT;
    } else {
        if (size < 64) *enclen = 1 + size;      // 6bit所能表示的无符号数，即size的最大致癌
        else if (size < 4096) *enclen = 2 + size;   // 12bit所能表示的无符号数，即size的最大值
        else *enclen = 5 + size;        // 32bit所能表示的无符号数，即size的最大值
        return LP_ENCODING_STRING;
    }
}

// 参数l是编码类型和实际数据的总长度，即entry-len字段的值
// 返回值是存储这个总长度所需要的字段长度(entry-len字段的长度)
// 参数buf是为了返回总长度的值，即entry-len字段的值，返回值也同样是buf的长度
/**
 * 如何判断entry-len是否结束？
 * 这就依赖于entry-len的编码方式了。entry-len每个字节的最高位，是用来标识当前字节是否为entry-len字段的最后一个字节，这里存在两种情况，分别是：
 *  - 最高位为1，表示entry-len还没有结束，当前字节的左边字节仍然是entry-len的内容
 *  - 最高位0，表示当前字节已经是entry-len最后一个字节了
 * 而entry-len每个字节的低7位，则记录了实际的长度信息。这里需要注意，entry-len采用大端模式存储，也就是说，entry-len的低位字节保存在内存高地址上。
 * */
unsigned long lpEncodeBacklen(unsigned char* buf, uint64_t l) {
    if (l <= 127) { // 7bit所能表示的无符号数
        if (buf) buf[0] == l;
        return 1;
    } else if(l < 16383) {  // 14bit所能表示的无符号数
        if (buf) {
            buf[0] = l >> 7;
            buf[1] = (l & 0x7F) | 0x80;     // 与0x7F 是为了只留后7位，前面都清零；然后 或0x80是把最高位置为1，表示entry-len还未结束
            return 2;
        }
    } else if (l < 2097151) {   // 21bit所能表示的无符号数
        if (buf) {
            buf[0] = l >> 14;
            buf[1] = ((l >> 7) & 0x7F) | 0x80;
            buf[2] = (l & 0x7F) | 0x80;
        }
        return 3;
    } else if (l < 268435455) { // 28bit所能表示的无符号数
        if (buf) {
            buf[0] = l >> 21;
            buf[1] = ((l >> 14) & 0x7F) | 0x80;
            buf[2] = ((l >> 7) & 0x7F) | 0x80;
            buf[3] = (l & 0x7F) | 0x80;
        }
        return 4;
    } else {    // 35bit所能表示的无符号数
        if (buf) {
            buf[0] = l >> 28;
            buf[1] = ((l >> 21) & 0x7F) | 0x80;
            buf[2] = ((l >> 14) & 0x7F) | 0x80;
            buf[3] = ((l >> 7) & 0x7F) | 0x80;
            buf[4] = (l & 0x7F) | 0x80;
        }
        return 5;
    }
}

// 获取p项的编码类型和实际数据的总长度
uint32_t lpCurrentEncodeSize(unsigned char* p) {
    if (LP_ENCODING_IS_7BIT_UINT(p[0])) return 1;
    if (LP_ENCODING_IS_13BIT_INT(p[0])) return 2;
    if (LP_ENCODING_IS_16BIT_INT(p[0])) return 3;
    if (LP_ENCODING_IS_24BIT_INT(p[0])) return 4;
    if (LP_ENCODING_IS_32BIT_INT(p[0])) return 5;
    if (LP_ENCODING_IS_64BIT_INT(p[0])) return 9;
    if (LP_ENCODING_IS_6BIT_STR(p[0])) return 1 + LP_ENCODING_6BIT_STR_LEN(p);
    if (LP_ENCODING_IS_12BIT_STR(p[0])) return 2 + LP_ENCODING_12BIT_STR_LEN(p);
    if (LP_ENCODING_IS_32BIT_STR(p[0])) return 5 + LP_ENCODING_32BIT_STR_LEN(p);
    if (p[0] == LP_EOF) return 1;
    return 0;
}

// 解码读取entry-len字段的值（编码+真实数据的总长度），从内高地址往低地址读取，因为是大端存储
uint64_t lpDecodeBacklen(unsigned char* p) {
    uint64_t val = 0;
    uint64_t shift = 0;

    do {
        val |= (uint64_t)(p[0] & 127) << shift; // 将字节的首位清零，它只是作为结束符用的，然后左移到对应的位置
        if (!(p[0] & 128)) break;   // 如果读取的字节，首位为0，表示该字节为entry-len的最后一个字节
        shift += 7; // shift标识读取的表示真实数据的bit数（去掉标识结束符的位）
        p--;    // 从高位往低位读取，大端存储
        if (shift > 28) return UINT64_MAX;  // 读取了5个字节了，还没有遇到首位为0的字节，还没结束，说明溢出了
    } while (1);

    return val;
}

// 对string类型数据进行编码
// buf输出字符串
// s输入字符串，len输入字符串长度
void lpEncodeString(unsigned char* buf, unsigned char* s, uint32_t len) {
    if (len < 64) { // 6bit所能表示的无符号数最大值
        buf[0] = len | LP_ENCODING_6BIT_STR;
        memcpy(buf + 1, s, len);
    } else if (len < 4096) {    // 12bit所能表示的无符号数最大值
        buf[0] = (len >> 8) | LP_ENCODING_12BIT_STR;
        buf[1] = len & 0xFF;
        memcpy(buf + 2, s, len);
    }  else {   // 32bit所能表示的无符号数最大值
        buf[0] = LP_ENCODING_32BIT_STR;
        buf[1] = len & 0xFF;
        buf[2] = (len >> 8) & 0xFF;
        buf[3] = (len >> 16) & 0xFF;
        buf[4] = (len >> 24) & 0xFF;
        memcpy(buf + 5, s, len);
    }
}

// p项的长度 = 编码类型长度(entry-encoding) + 实际数据长度(entry-data) + 存储 编码类型长度和实际数据长度之和 的字段长度(entry-len)
// 跳过p项，获取下一项
unsigned char* lpSkip(unsigned char* p) {
    unsigned long entrylen = lpCurrentEncodeSize(p);
    entrylen += lpEncodeBacklen(NULL, entrylen);
    p += entrylen;
    return p;
}

// 获取p项的下一项，如果是结尾返回NULL
unsigned char* lpNext(unsigned char* lp, unsigned char* p) {
    ((void) lp);
    p = lpSkip(p);
    if (p[0] == LP_EOF) return NULL;
    return p;
}

// 获取p项的前一项，如果p项就是第一项，则返回NULL
unsigned char* lpPrev(unsigned char* lp, unsigned char* p) {
    if (p - lp == LP_HDR_SIZE) return NULL;
    p--;    // 指向前一项的entry-len字段的最高地址，因为entry-len的数值是大端存储，所以这里最高地址就是数值的最低位
    uint64_t prevlen = lpDecodeBacklen(p);  // 获取前一项的编码和真实数据的总长度，即entry-len字段的值
    prevlen += lpEncodeBacklen(NULL, prevlen);  // 获取前一项entry-len字段的长度
    return p - prevlen + 1;
}

// 第一个元素
unsigned char* lpFirst(unsigned char* lp) {
    lp += LP_HDR_SIZE;
    if (lp[0] == LP_EOF) return NULL;
    return lp;
}

// 第二个元素
unsigned char *lpLast(unsigned char* lp) {
    // -1是为了跳过结尾标识符
    unsigned char* p = lp + lpGetTotalBytes(lp) - 1;
    return lpPrev(lp, p);
}

// 获取lp中元素个数
uint32_t lpLength(unsigned char* lp) {
    uint32_t numele = lpGetNumElements(lp);

    if (numele != LP_HDR_NUMELE_UNKNOWN) return numele;

    // 重新获取真实的元素数量
    uint32_t count = 0;
    unsigned char *p = lpFirst(p);
    while (p) {
        count++;
        p = lpNext(lp, p);
    }
    if (count < LP_HDR_NUMELE_UNKNOWN)  lpSetNumElements(lp, count);
    return count;
}

// 获取元素值
// 对于字符串编码的元素：参数count返回字符串的长度，函数返回值是字符串的首地址
// 对于int编码的元素：如果参数intbuf为NULL，则参数count返回的就是元素的值；
//  如果参数intbuf不为NULL，则函数返回值是将int编码元素转换成string之后字符串首地址（intbuf参数也返回了该内容），count表示返回的字符串的长度
unsigned char* lpGet(unsigned char* p, int64_t* count, unsigned char* intbuf) {
    int64_t val;
    uint64_t uval, negstart, negmax;

    if (LP_ENCODING_IS_7BIT_UINT(p[0])) {
        negstart = UINT64_MAX;  // 7bit存储的都是无符号数，没有负值
        negmax = 0;
        uval = p[0] & 0x7F;
    } else if (LP_ENCODING_IS_13BIT_INT(p[0])) {
        uval = ((p[0] & 0x1F) << 8) | p[1];
        negstart = (uint64_t)1 << 12;
        negmax = 8191;
    } else if (LP_ENCODING_IS_16BIT_INT(p[0])) {
        uval = (uint64_t)p[1] | (uint64_t)p[2] << 8;
        negstart = (uint64_t) 1 << 15;
        negmax = UINT16_MAX;
    } else if (LP_ENCODING_IS_24BIT_INT(p[0])) {
        uval = (uint64_t)p[1] | (uint64_t)p[2] << 8 | (uint64_t)p[3] << 16;
        negstart = (uint64_t) 1 << 23;
        negmax = UINT32_MAX >> 8;
    } else if (LP_ENCODING_IS_32BIT_INT(p[0])) {
        uval = (uint64_t)p[1] | (uint64_t)p[2] << 8 | (uint64_t)p[3] << 16 | (uint64_t)p[4] << 24;
        negstart = (uint64_t) 1 << 31;
        negmax = UINT32_MAX;
    } else if (LP_ENCODING_IS_64BIT_INT(p[0])) {
        uval = (uint64_t)p[1] | (uint64_t)p[2] << 8 | (uint64_t)p[3] << 16 | (uint64_t)p[4] << 24
                | (uint64_t)p[5] << 32 | (uint64_t)p[6] << 40 | (uint64_t)p[7] << 48 | (uint64_t)p[8] << 56;
        negstart = (uint64_t) 1 << 63;
        negmax = UINT64_MAX;
    } else if (LP_ENCODING_IS_6BIT_STR(p[0])) {
        *count = LP_ENCODING_6BIT_STR_LEN(p);
        return p + 1;
    } else if (LP_ENCODING_IS_12BIT_STR(p[0])) {
        *count = LP_ENCODING_12BIT_STR_LEN(p);
        return p + 2;
    } else if (LP_ENCODING_IS_32BIT_STR(p[0])) {
        *count = LP_ENCODING_32BIT_STR_LEN(p);
        return p + 5;
    } else {
        uval = 12345678900000000ULL + p[0];
        negstart = UINT64_MAX;
        negmax = 0;
    }

    if (uval >= negstart) {
        uval = negmax - uval;
        val = uval;
        val = -val - 1;
    } else {
        val = uval;
    }

    if (intbuf) {
        /**
         * 函数原型：int snprintf(char* dest_str,size_t size,const char* format,...);
         * 将可变个参数(...)按照format格式化成字符串，然后将其复制到str中。
         *   (1) 如果格式化后的字符串长度 < size，则将此字符串全部复制到str中，并给其后添加一个字符串结束符('\0')；
         *   (2) 如果格式化后的字符串长度 >= size，则只将其中的(size-1)个字符复制到str中，并给其后添加一个字符串结束符('\0')，返回值为欲写入的字符串长度。
         * 若成功则返回预写入的字符串长度，若出错则返回负值。
         * */
         *count = snprintf((char*)intbuf, LP_INTBUF_SIZE, "%lld", (long long )val);
        return intbuf;
    } else {
        *count = val;
        return NULL;
    }
}

// 插入元素
// ele size 元素和元素大小
// where插入的位置
// p在哪个元素前后插入
// newp返回插入的元素
// 函数返回值是插入后listpack的首地址，因为插入过程中可能涉及内存的重新分配，lp的地址可能会发生改变
unsigned char* lpInsert(unsigned char* lp, unsigned char* ele, uint32_t size, unsigned char* p, int where, unsigned char** newp) {
    unsigned char intenc[LP_MAX_INT_ENCDOING_LEN];
    unsigned char backlen[LP_MAX_BACKLEN_SIZE];

    uint64_t enclen;

    // ele为空，表示要把p项删除
    if (ele == NULL) where = LP_REPLACE;

    // 将之后插入转成在下一个元素之前插入
    if (where == LP_AFTER) {
        p = lpSkip(p);
        where = LP_BEFORE;
    }

    unsigned long poff = p - lp;    // 保存p项相对于lp开头的编译，因为重新分配内存之后，可能p项的内存地址会变化

    int enctype;
    // 对元素进行编码
    if (ele) {
        // intenc整型编码结果，enclen表示编码+真实数据的总长度
        // enctype表示是int编码还是string编码
        enctype = lpEncodeGetType(ele, size, intenc, &enclen);
    } else {
        enctype = -1;
        enclen = 0;
    }

    // 根据enclen获取entry-len字段的长度
    unsigned long backlen_size = ele ? lpEncodeBacklen(backlen, enclen) : 0;

    uint64_t old_listpack_bytes = lpGetTotalBytes(lp);
    uint32_t replaced_len = 0;
    if (where == LP_REPLACE) {  // 如果是替换，即把p项的内容替换成ele，操作方式是，先删除p项，然后再新插入ele
        replaced_len = lpCurrentEncodeSize(p);
        replaced_len += lpEncodeBacklen(NULL, replaced_len);
        // replaced_len是p项的总长度
    }

    // 计算插入后新的listpack总长度
    // enclen + backlen_size 是插入项的总长度
    // replaced_len是要删除项的总长度（如果ele为NULL）
    uint64_t new_listpack_bytes = old_listpack_bytes + enclen + backlen_size - replaced_len;
    if (new_listpack_bytes > UINT32_MAX) return NULL;

    unsigned char* dst = lp + poff; // 老的p项的地址

    // 需要更多内存，申请
    if (new_listpack_bytes > old_listpack_bytes) {  // 需要更多的内存
        if ((lp = lp_realloc(lp, new_listpack_bytes)) == NULL) return NULL;
        dst = lp + poff;    // 重新分配内存之后新的p项地址，即插入的位置
    }

    // 在p项之前插入，插入位置是dst(起始就是p指向的位置)
    if (where == LP_BEFORE) {
        // 将p项以及其后面的所有元素 往后移动一个单位，单位为插入元素的长度，空出插入元素的位置
        memmove(dst + enclen + backlen_size, dst, old_listpack_bytes - poff);
    } else {    // LP_REPLACE
        long lendiff = (enclen + backlen_size) - replaced_len;  // 替换前后的长度差异
        // 将p项之后的元素（不包括p项）往后移动一个单位，单位是p项的总长度+替换前后差异的长度，空出需要新插入元素的位置（替换操作：先删除，再插入）
        memmove(dst + replaced_len + lendiff, dst + replaced_len, old_listpack_bytes - poff + replaced_len);
    }

    // 有多余的内存，释放
    if (new_listpack_bytes < old_listpack_bytes) {  // 有多余的内存
        if ((lp = lp_realloc(lp, new_listpack_bytes)) == NULL) return NULL;
        dst = lp + poff;    // 重新分配内存之后插入元素的位置(如果ele为NULL，则是删除元素之后的位置，即被删除元素下一项的位置)，上面内存移动已经空出来了该元素的空间
    }

    // 存储新的元素
    if (newp) {
        *newp = dst; // 设置返回的新元素的指针
        if (!ele && dst[0] == LP_EOF) *newp = NULL;
    }
    if (ele) {
        if (enctype == LP_ENCODING_INT) {
            // int编码，编码值和真实数据都在一起，都存在intenc中
            memcpy(dst, intenc, enclen);
        } else {
            // string编码，直接将编码值和元素内容，存放在对应的位置
            lpEncodeString(dst, ele, size);
        }
        dst += enclen;  // 跳过编码字段和真实数据字段
        // 设置entry-len字段
        memcpy(dst, backlen, backlen_size);
        dst += backlen_size;
    }

    // 更新listpack头
    if (where != LP_REPLACE || ele == NULL) {
        uint32_t num_elements = lpGetNumElements(lp);
        if (num_elements != LP_HDR_NUMELE_UNKNOWN) {
            if (ele)
                lpSetNumElements(lp, num_elements+1);
            else    // ele为空，表示删除p项
                lpSetNumElements(lp, num_elements-1);
        }
    }
    lpSetTotalBytes(lp, new_listpack_bytes);

    return lp;
}

unsigned char* lpAppend(unsigned char* lp, unsigned char* ele, uint32_t size) {
    uint64_t listpack_bytes = lpGetTotalBytes(lp);
    // 定位到lp结尾
    unsigned char* eofptr = lp + listpack_bytes - 1;
    return lpInsert(lp, ele, size, eofptr, LP_BEFORE, NULL);
}

unsigned char* lpDelete(unsigned char* lp, unsigned char* p, unsigned char** newp) {
    return lpInsert(lp, NULL, 0, p, LP_REPLACE, newp);
}

uint32_t lpBytes(unsigned char* lp) {
    return lpGetTotalBytes(lp);
}

// 查看lp索引为index的项
unsigned char* lpSeek(unsigned char* lp, long index) {
    int forward = 1;    // 默认从前往后找

    uint32_t numele = lpGetNumElements(lp);
    if (numele != LP_HDR_NUMELE_UNKNOWN) {
        // index小于0，从后往前第-index项
        if (index < 0) index = (long)numele + index;
        if (index < 0) return NULL;
        if (index >= numele) return NULL;

        if (index > numele/2) { // 如果index的位置是在后半部分，就从后往前找
            forward = 0;
            index -= numele;
        }
    } else {
        // 如果listpack的元素数量未定义，则负的index就直接从后往前找，不会去判断该索引是在前半部分还是后半部分
        if (index < 0) forward = 0;
    }

    if (forward) {  // 从前往后
        unsigned char* ele = lpFirst(lp);
        while (index > 0 && ele) {
            ele = lpNext(lp, ele);
            index--;
        }
        return ele;
    } else {    // 从后往前
        unsigned char* ele = lpLast(lp);
        while (index < -1 && ele) {
            ele = lpPrev(lp, ele);
            index++;
        }
        return ele;
    }
}