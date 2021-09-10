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

// 获取zl中p项的下一项
unsigned char* ziplistNext(unsigned char* zl, unsigned char* p);
// 获取zl中p项的前一项
unsigned char* ziplistPrev(unsigned char* zl, unsigned char* p);
// 查询ziplist中元素的数量
unsigned int ziplistLen(unsigned char* zl);
// 拼接两个zl，拼接完之后，first中的元素在前，second中的元素在后
unsigned char* ziplistMerge(unsigned char** first, unsigned char** second);

// 增
unsigned char* ziplistInsert(unsigned char* zl, unsigned char* p, unsigned char* s, unsigned int slen);
unsigned char* ziplistPush(unsigned char* zl, unsigned char* s, unsigned int slen, int where);
// 删
unsigned char* ziplistDelete(unsigned char* zl, unsigned char** p);
unsigned char* ziplistDeleteRange(unsigned char* zl, int index, unsigned int num);
// 查
unsigned char* ziplistFind(unsigned char* p, unsigned char* vstr, unsigned int vlen, unsigned int skip);
unsigned int ziplistCompare(unsigned char* p, unsigned char* sstr, unsigned int slen);

// 获取ziplist项中存储的值
unsigned int ziplistGet(unsigned char* p, unsigned char** sstr, unsigned int* slen, long long *sval);
#endif //REDIS_ZIPLIST_H
