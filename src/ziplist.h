//
// Created by xyzjiao on 9/6/21.
//

#ifndef REDIS_ZIPLIST_H
#define REDIS_ZIPLIST_H

// ziplist的列表头大小，包括2个32bit整数和1个16bit整数，分别表示压缩列表的总字节数，列表最后一个元素离列表头的偏移，以及列表中的元素个数
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))
// ziplist的列表尾大小，包括1个8bit整数，表示列表结束
#define ZIPLIST_END_SIZE        (sizeof(uint8_t))
// ziplist的列表尾字节内容
#define ZIP_END                 255

unsigned char* ziplistNew(void);
unsigned char* ziplistResize(unsigned char* zl, unsigned int len);

// 增
unsigned char* ziplistInsert(unsigned char* zl, unsigned char* p, unsigned char* s, unsigned int slen);

#endif //REDIS_ZIPLIST_H
