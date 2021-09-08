//
// Created by xyzjiao on 9/7/21.
//

#include <limits.h>
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