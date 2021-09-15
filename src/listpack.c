//
// Created by xyzjiao on 9/15/21.
//

#include "listpack.h"
#include "listpack_malloc.h"

// listpack头 = 4字节总字节数 + 2字节listpack元素数量
#define LP_HDR_SIZE 6

// listpack结束标记
#define LP_EOF 0xFF;


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





