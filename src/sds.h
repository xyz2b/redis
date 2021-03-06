//
// Created by xyzjiao on 8/22/21.
//

#ifndef REDIS_SDS_H
#define REDIS_SDS_H

#define SDS_MAX_REALLOC (1024 * 1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

typedef char* sds;

// 字符串长度在一个字节所能表示范围内
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t     len;            // 字符数组buf已使用的长度，size
    uint8_t     alloc;          // 字符数组buf分配的长度，capacity
    unsigned char flags;        // sds类型，表示字符串应该用什么结构来存储，根据字符串的长度来决定
    char        buf[];          // 字符数组，真正存储字符串的地方
};

// 字符串长度在两个字节所能表示范围内
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t     len;            // 字符数组buf已使用的长度，size
    uint16_t     alloc;          // 字符数组buf分配的长度，capacity
    unsigned char flags;        // sds类型，表示字符串应该用什么结构来存储，根据字符串的长度来决定
    char        buf[];          // 字符数组，真正存储字符串的地方
};

// 字符串长度在四个字节所能表示范围内
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t     len;            // 字符数组buf已使用的长度，size
    uint32_t     alloc;          // 字符数组buf分配的长度，capacity
    unsigned char flags;        // sds类型，表示字符串应该用什么结构来存储，根据字符串的长度来决定
    char        buf[];          // 字符数组，真正存储字符串的地方
};

// 字符串长度在八个字节所能表示范围内
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t     len;            // 字符数组buf已使用的长度，size
    uint64_t     alloc;          // 字符数组buf分配的长度，capacity
    unsigned char flags;        // sds类型，表示字符串应该用什么结构来存储，根据字符串的长度来决定
    char        buf[];          // 字符数组，真正存储字符串的地方
};
// 未指明长度的数组在结构体中所占用的长度为0
// sizeof(sdshdr64): 8 + 8 + 1 + 0 = 17
// char        buf[];  --> char        buf[4];
// sizeof(sdshdr64): 8 + 8 + 1 + 4 = 21


#define SDS_TYPE_8      1       // 字符串长度在一个字节所能表示范围内
#define SDS_TYPE_16     2       // 字符串长度在两个字节所能表示范围内
#define SDS_TYPE_32     3       // 字符串长度在四个字节所能表示范围内
#define SDS_TYPE_64     4       // 字符串长度在八个字节所能表示范围内
#define SDS_TYPE_MASK   7       // 0111 111 掩码

// sdshdr的首地址，是真正存储字符串的位置往前一个sdshdr的大小
#define SDS_HDR_VAR(T, s) struct sdshdr##T * sh = (void *) ((s) - (sizeof(struct sdshdr##T)));
// 根据真实字符串的位置s，获取sds结构体的首地址，指针往前移动结构大小个位置即可
#define SDS_HDR(T, s) ((struct sdshdr##T *) ((s) - (sizeof(struct sdshdr##T))))

// 根据创建时返回的真实存储字符串的位置，来获取sds字符串的长度
// 根据创建时返回的真实存储字符串的位置可以获取到对应的sds结构体的首地址
static inline size_t sdslen(const sds s) {
    // sds中flags字段位置位于真实存储字符串位置的前一个位置
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->len;
        default:
            return 0;
    }
}

// 设置sds字符串的长度
static inline void sdssetlen(sds s, size_t newlen) {
    // sds中flags字段位置位于真实存储字符串位置的前一个位置
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_8:
            SDS_HDR(8, s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->len = newlen;
            break;
    }
}

// 获取sds中可用的存储字符串的空间
static inline size_t sdsavail(const sds s) {
    // sds中flags字段位置位于真实存储字符串位置的前一个位置
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->alloc - SDS_HDR(8, s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->alloc - SDS_HDR(16, s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->alloc - SDS_HDR(32, s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->alloc - SDS_HDR(64, s)->len;
        default:
            return 0;
    }
}

// 设置sds的alloc
static inline void sdssetalloc(const sds s, size_t newlen) {
    // sds中flags字段位置位于真实存储字符串位置的前一个位置
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_8:
            SDS_HDR(8, s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16, s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32, s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64, s)->alloc = newlen;
            break;
    }
}

// 获取sds的alloc
static inline size_t sdsalloc(const sds s) {
    // sds中flags字段位置位于真实存储字符串位置的前一个位置
    unsigned char flags = s[-1];
    switch (flags & SDS_TYPE_MASK) {
        case SDS_TYPE_8:
            return SDS_HDR(8, s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16, s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32, s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64, s)->alloc;
        default:
            return 0;
    }
}

// 新建
sds sdsnewlen(const void* init, size_t initlen);
sds sdsnew(const char *init);
sds sdsempty(void);

// 复制
sds sdsdup(const sds s);

// 释放
void sdsfree(sds s);

// 扩容
sds sdsMakeRoomFor(sds s, size_t addlen);

// 字符串拼接
sds sdscatlen(sds s, const void *t, size_t len);
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);

// 字符串赋值
sds sdscpylen(sds s, const char *t, size_t len);
sds sdscpy(sds s, const char *t);

// 字符串处理
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, ssize_t start, ssize_t end);

// 更新len
void sdsupdatelen(sds s);

// 清空sds字符串
void sdsclear(sds s);

// 比较两个sds字符串
int sdscmp(const sds s1, const sds s2);

// C字符串分割
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);

// 大小写转换
void sdstolower(sds s);
void sdstoupper(sds s);

// 根据数值创建sds字符串
sds sdsfromlonglong(long long value);

// 字符串拼接
sds sdsjoin(char **argv, int argc, char *sep);
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);


sds sdsRemoveFreeSpace(sds s);

#endif //REDIS_SDS_H