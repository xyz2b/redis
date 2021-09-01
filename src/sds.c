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

// 扩容sds，addlen是需要扩容的大小
sds sdsMakeRoomFor(sds s, size_t addlen) {
    void *sh, *newsh;
    size_t len, newlen;
    int hdrlen;


    // 获取原先的sds type
    char type, oldtype = s[-1] & SDS_TYPE_MASK;
    // 获取sds结构体指针
    sh = (char *)(s - sdsHdrSize(oldtype));


    // 1.获取剩余空间
    size_t avail = sdsavail(s);
    // 剩余空间大于需要增加的扩容，不需要扩容
    if (avail > addlen) return s;

    // 2.需要扩容的空间
    len = sdslen(s);
    newlen = (len + addlen);
    if (newlen < SDS_MAX_REALLOC)
        newlen *= 2;
    else
        newlen += SDS_MAX_REALLOC;

    // 3.判断扩容之后的新长度所需要的sds结构体类型
    type = sdsReqType(newlen);

    // 4.扩容之后的sds类型和之前的类型是否相同
    // 相同，表示扩容前后sds结构体相同，可以使用realloc重新分配内存
    // 不相同，表示扩容前后sds结构体不相同，不能使用realloc，只能使用malloc重新分配内存
    hdrlen = sdsHdrSize(type);
    if (oldtype == type) {
        newsh = s_realloc(sh, hdrlen + newlen + 1);
        if (newsh == NULL) return NULL;
        s = (char *)newsh + hdrlen;
    } else {
        newsh = s_malloc(hdrlen + newlen + 1);
        if (newsh == NULL) return NULL;
        // copy原来的sds字符串到新的sds中
        memcpy((char *)newsh + hdrlen, s, len + 1);
        // 释放原先的sds
        s_free(sh);
        s = (char *)newsh + hdrlen;
        s[-1] = type;
        // 因为存储的字符串没有发生变化，所以标识字符串长度的len也没有发生变化，只是alloc变大了
        sdssetlen(s, len);
    }
    // 设置新的alloc
    sdssetalloc(s, newlen);
    return s;
}

// 将给定C字符串拼接到sds字符串的末尾
sds sdscatlen(sds s, const void *t, size_t len) {
    size_t curlen = sdslen(s);

    // 会导致sds字符串长度改变的操作都调用该函数，进行扩容判断与扩容处理
    s = sdsMakeRoomFor(s, len);
    if (s == NULL) return NULL;
    // 将c字符串copy到原有sds字符串的末尾
    memcpy(s+curlen, t, len);
    sdssetlen(s, curlen + len);
    s[curlen + len] = '\0';
    return s;
}