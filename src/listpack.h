//
// Created by xyzjiao on 9/15/21.
//

#ifndef REDIS_LISTPACK_H
#define REDIS_LISTPACK_H

// 64位有符号数所能表示的最大正数有19位。转成字符串就一个数值位就是一个字符，每个数值位占一个字节
// 同时负数转成字符串又要多一个负号字符('-')，又多一个字节
// 最后由于C字符串要有结束标识符'\0'，又多一个字节，所以总共能够表示long long类型数值的字符串需要有21个字节
#define LP_INTBUF_SIZE 21


#define LP_BEFORE 0
#define LP_AFTER 1
#define LP_REPLACE 2

unsigned char* lpNew(void);
void lpFree(unsigned char* lp);
unsigned char *lpInsert(unsigned char *lp, unsigned char *ele, uint32_t size, unsigned char *p, int where, unsigned char **newp);
unsigned char *lpAppend(unsigned char *lp, unsigned char *ele, uint32_t size);
unsigned char *lpDelete(unsigned char *lp, unsigned char *p, unsigned char **newp);
uint32_t lpLength(unsigned char *lp);
unsigned char *lpGet(unsigned char *p, int64_t *count, unsigned char *intbuf);
unsigned char *lpFirst(unsigned char *lp);
unsigned char *lpLast(unsigned char *lp);
unsigned char *lpNext(unsigned char *lp, unsigned char *p);
unsigned char *lpPrev(unsigned char *lp, unsigned char *p);
uint32_t lpBytes(unsigned char *lp);
unsigned char *lpSeek(unsigned char *lp, long index);

#endif //REDIS_LISTPACK_H
