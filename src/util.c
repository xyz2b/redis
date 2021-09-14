//
// Created by xyzjiao on 9/7/21.
//

#include <limits.h>
#include <stdint.h>
#include "util.h"

// 将string 转成long long整形，转换成功返回1，转换后的值由value返回
int string2ll(const char* s, size_t slen, long long* value) {
    const char* p = s;
    size_t plen = 0;    // 目前已解析的长度
    int negative = 0;   // 标识解析出来的数值正负
    unsigned long long v;

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
        if(v > (ULLONG_MAX / 10))   // 溢出，因为后v要乘以10，所以这里要除以10
            return 0;

        v *= 10;

        if (v > (ULLONG_MAX - (p[0] - '0')))    // 溢出，因为后面v要加上(p[0] - '0')，所以这里需要减掉
            return 0;

        v += p[0] - '0';
        p++; plen++;
    }

    // 不是所有字符都是数值
    if(plen < slen)
        return 0;

    if (negative) {
        // 有符号整数，比如char，一个字节，所能表示的值为-128 -> 127，所以对(-(-128+1)+1)=128，就是负数相反的正数，如果负数去除负号之后的值大于这个值，就是负值溢出了
        if (v > (unsigned long long)(-(LLONG_MIN+1) + 1))   // 溢出
            return 0;
        if (value != NULL) *value = -v;
    } else {
        if (v > LLONG_MAX)  // 溢出
            return 0;
        if (value != NULL) *value = v;
    }

    return 1;
}

uint32_t digits10(uint64_t v) {
    if (v < 10) return 1;
    if (v < 100) return 2;
    if (v < 1000) return 3;
    if (v < 1000000000000UL) {
        if (v < 100000000UL) {
            if (v < 1000000) {
                if (v < 10000) return 4;
                return 5 + (v >= 100000);
            }
            return 7 + (v >= 10000000UL);
        }
        if (v < 10000000000UL) {
            return 9 + (v >= 1000000000UL);
        }
        return 11 + (v >= 100000000000UL);
    }
    return 12 + digits10(v / 1000000000000UL);
}

int ll2string(char *dst, size_t dstlen, long long svalue) {
    static const char digits[201] =
            "0001020304050607080910111213141516171819"
            "2021222324252627282930313233343536373839"
            "4041424344454647484950515253545556575859"
            "6061626364656667686970717273747576777879"
            "8081828384858687888990919293949596979899";
    int negative;
    unsigned long long value;

    /* The main loop works with 64bit unsigned integers for simplicity, so
     * we convert the number here and remember if it is negative. */
    if (svalue < 0) {
        if (svalue != LLONG_MIN) {
            value = -svalue;
        } else {
            value = ((unsigned long long) LLONG_MAX)+1;
        }
        negative = 1;
    } else {
        value = svalue;
        negative = 0;
    }

    /* Check length. */
    uint32_t const length = digits10(value)+negative;
    if (length >= dstlen) return 0;

    /* Null term. */
    uint32_t next = length;
    dst[next] = '\0';
    next--;
    while (value >= 100) {
        int const i = (value % 100) * 2;
        value /= 100;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
        next -= 2;
    }

    /* Handle last 1-2 digits. */
    if (value < 10) {
        dst[next] = '0' + (uint32_t) value;
    } else {
        int i = (uint32_t) value * 2;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
    }

    /* Add sign. */
    if (negative) dst[0] = '-';
    return length;
}