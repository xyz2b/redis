//
// Created by xyzjiao on 8/22/21.
//

#include <string.h>
#include "sds.h"
#include "sdsalloc.h"

static inline int sdsHdrSize(char type) {
    switch (type) {
        case SDS_TYPE_8:
            return sizeof(struct sdshdr8);
        case SDS_TYPE_16:
            return sizeof(struct sdshdr16);
        case SDS_TYPE_32:
            return sizeof(struct sdshdr32);
        case SDS_TYPE_64:
            return sizeof(struct sdshdr64);
        default:
            return 0;
    }
}

static inline char sdsReqType(size_t string_size) {
    if (string_size < 0xff)       // 2的8次方，即uint8_t 一个字节所能表示的范围
        return SDS_TYPE_8;
    if (string_size < 0xffff)     // 2的16次方，即uint16_t 两个字节所能表示的范围
        return SDS_TYPE_16;
    if (string_size < 0xffffffff)   // 2的32次方，即uint32_t 四个字节所能表示的范围
        return SDS_TYPE_32;

    return SDS_TYPE_64;             // 2的64次方，即uint64_t 八个字节所能表示的范围
}

// 根据init指针指向的字符串内容以及字符串长度创建sds字符串
// 返回sds中真正存储字符串的位置，同样是个c字符串（有结束标记符'\0'），为了兼容c字符串处理函数
sds sdsnewlen(const void* init, size_t initlen) {
    void* sh;       // 指向sds结构体的指针
    sds   s;        // sds类型变量，即char*字符数组
    char type = sdsReqType(initlen);        // 获取所能保存该长度字符串的sds类型

    if (initlen == 0) type = SDS_TYPE_8;    // 空字符串初始化sds为SDS_TYPE_8类型

    // 计算sds头的大小，不包括真正存储字符串的区域
    int hdrlen = sdsHdrSize(type);
    unsigned char* fp;  // 指向flags的指针

    // 分配sds头和存储字符串的内存
    // +1是为了存储字符串结束标记'\0'，兼容c字符串
    sh = s_malloc(hdrlen + initlen + 1);

    // 初始化sds的内存为0
    memset(sh, 0, hdrlen + initlen + 1);

    // 申请内存失败
    if (sh == NULL) return NULL;

    s = (char *) sh +hdrlen;    // 将sds变量指向sds中真正存储字符串的位置

    fp = ((unsigned char *)s) - 1;  // flag位于sds中真正存储字符串的位置的前一个位置，将fp指针指向flag的真实位置，方便读写

    // 转换sh的类型为指定type的sds结构体类型，并做一些初始化工作
    switch (type) {
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8, s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16, s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32, s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64, s);
            sh->len = initlen;
            sh->alloc = initlen;
            *fp = type;
            break;
        }
    }

    // 如果传入的字符串不为空，将字符串的实际内容，copy到sds结构中
    if (init && initlen) {
        memcpy(s, init, initlen);
    }
    // 字符串结束标记符'\0'
    s[initlen] = '\0';

    // 返回真正存储字符串的地址，就是c字符串
    return s;
}

// 根据c字符串创建sds
sds sdsnew(const char *init) {
    size_t initlen = (init == NULL) ? 0 : strlen(init);
    return sdsnewlen(init, initlen);
}

// 创建空字符传的sds
sds sdsempty(void) {
    return sdsnewlen("", 0);
}

// 复制sds，产生一份新的
sds sdsdup(const sds s) {
    return sdsnewlen(s, sdslen(s));
}

// 释放sds
void sdsfree(sds s) {
    if (s == NULL) return;
    // 定位到sds结构体首地址指针，调用free库函数
    // 当初申请内存时，就是直接申请的一块连续内存，结构体+字符串，所以只需要定位到结构的首地址，传给free库函数即可
    // s[-1]定位到flags的位置，然后使用sdsHdrSize获取对应类型的结构体大小
    // s(真正存储字符串的位置)减去结构体大小即获取到结构体首地址
    s_free((char *)s - sdsHdrSize(s[-1]));
}

// 用空字符串将sds扩展至指定长度
sds sdsgrowzero(sds s, size_t len) {
    size_t curlen = sdslen(s);

    if (len <= curlen) return s;
    // 扩容
    s = sdsMakeRoomFor(s, len - curlen);
    if (s == NULL) return NULL;

    // 将扩容部分设置为0空字符
    memset(s+curlen , 0, len - curlen);
    // 设置库容后的长度
    sdssetlen(s, len);
    return s;
}

// 扩容sds
sds sdsMakeRoomFor(sds s, size_t addlen) {

}