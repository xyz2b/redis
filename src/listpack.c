//
// Created by xyzjiao on 9/15/21.
//

#include <inttypes.h>
#include "listpack.h"
#include "listpack_malloc.h"

// listpack头 = 4字节总字节数 + 2字节listpack元素数量
#define LP_HDR_SIZE 6

// listpack结束标记
#define LP_EOF 0xFF;

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
#define LP_ENCODING_13BIT_UINT 0xC0
// 1110 0000
#define LP_ENCODING_13BIT_UINT_MASK 0xE0
#define LP_ENCODING_IS_13BIT_UINT(byte) (((byte) & LP_ENCODING_13BIT_UINT_MASK) == LP_ENCODING_13BIT_UINT)

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
                                        ((uint32_t)(p)[2] << 8) \
                                        ((uint32_t)(p)[3] << 16)\
                                        ((uint32_t)(p)[4] << 24))


#define lpSetTotalBytes(p, v) do { \
    (p)[0] = (v) & 0xff;           \
    (p)[1] = ((v)>>8) & 0xff;      \
    (p)[2] = ((v)>>16) & 0xff;     \
    (p)[3] = ((v)>>24) & 0xff;\
                                   \
} while(0)

#define lpSetElements(p, v) do { \
    (p)[4] = (v) & 0xff;         \
    (p)[5] = ((v)>>8) & 0xff;\
} while(0)

unsigned char* lpNew(void) {
    // +1 是一字节的结尾标识
    unsigned char* lp = lp_malloc(LP_HDR_SIZE + 1);
    if (lp == NULL) return NULL;
    lpSetTotalBytes(lp, LP_HDR_SIZE+1);
    lpSetElements(lp, 0);
    lp[LP_HDR_SIZE] = LP_EOF;
    return lp;
}

void lpFree(unsigned char* lp) {
    lp_free(lp);
}

// 将string 转成int64整形，转换成功返回1，转换后的值由value返回
int string2ll(const char* s, size_t slen, long long* value) {
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

}
