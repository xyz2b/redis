//
// Created by xyzjiao on 8/22/21.
//

#include <string.h>
#include "sds.h"
#include "sdsalloc.h"

// 获取不同type的sds结构体的大小
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

// 根据字符串的长度获取需要使用什么类型的sds结构体来存储
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
// 第二个参数是相对于当前sds的len需要增加的大小，而不是相对于当前sds的alloc需要增加的大小
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

// 将给定字符串拼接到sds字符串的末尾，指定长度
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

// 将给定C字符串拼接到sds字符串的末尾
sds sdscat(sds s, const char *t) {
    return sdscatlen(s, t, strlen(t));
}

// 将给定sds字符串拼接到sds字符串的末尾
sds sdscatsds(sds s, const sds t) {
    return sdscatlen(s, t, sdslen(t));
}

// 将给定字符串赋值给sds字符串，指定长度
sds sdscpylen(sds s, const char *t, size_t len) {
    // 判断sds现有的空间是否能够存储给定的字符串
    if (sdsalloc(s) < len) {
        // 如果不够就扩容
        // sdsMakeRoomFor传入的第二个参数是相对于当前sds的len需要增加的大小
        s = sdsMakeRoomFor(s, len - sdslen(s));
        if (s == NULL) return NULL;
    }
    // 将字符串copy到sds字符串中
    memcpy(s, t, len);
    s[len] = '\0';
    sdssetlen(s, len);
    return s;
}

// 将c字符串赋值给sds字符串
sds sdscpy(sds s, const char *t) {
    return sdscpylen(s, t, strlen(t));
}

// 将sds字符串中连续包含的指定C字符串中的字符删除
sds sdstrim(sds s, const char *cset) {
    char *start, *end, *sp, *ep;
    size_t len;

    sp = start = s;
    ep = end = s + sdslen(s) - 1;
    // 从左到右遍历sds中的字符，看是否在C字符串中，直到第一个不在C字符串中的字符为止
    while (sp <= end && strchr(cset, *sp)) sp++;
    // 从右到左遍历sds中的字符，看是否在C字符串中，直到第一个不在C字符串中的字符为止
    while (ep > sp && strchr(cset, *ep)) ep--;
    // 删除之后的长度，如果sp大于ep，说明原本的sds字符串被删除完了，长度为0，否则长度就是(ep - sp) + 1
    len = (sp > ep) ? 0 : ((ep - sp) + 1);
    // 如果s == sp，说明原sds字符串开头位置的字符没有在C字符串中的，即开头位置没有变化，此时不需要移动内存位置，直接修改len即可
    if (s != sp) memmove(s, sp, len);
    s[len] = '\0';
    sdssetlen(s ,len);
    return s;
}

// 字符串切割
void sdsrange(sds s, ssize_t start, ssize_t end) {
    size_t newlen, len = sdslen(s);

    if (len == 0) return;

    // 处理start为负的情况
    if (start < 0) {
        start = len + start;
        // 如果负值超出了当前sds的长度，直接将start置为0
        if (start < 0) start = 0;
    }

    // 处理end为负的情况
    if (end < 0) {
        end = len + end;
        // 如果负值超出了当前的sds的长度，直接将end置为0
        if (end < 0) end = 0;
    }

    // 如果start大于end，截取的字符串大小为0，即截取出来的字符串是空字符串（将空字符串赋值给当前sds字符串）；否则为(end-start) + 1
    newlen = (start > end) ? 0 : (end - start) + 1;
    // 如果截取的字符串大小不为0
    if (newlen != 0) {
        // start大于sds字符串的长度，表明截取的字符串大小为0，即截取出来的字符串是空字符串（将空字符串赋值给当前sds字符串）
        if (start >= (ssize_t)len) {
            newlen = 0;
        } else if (end >= (ssize_t)len) {   // end大于sds字符串的长度，则将end设置为字符串最后一个字符的位置
            end = len - 1;
            // 如果start大于end，表明截取的字符串大小为0，即截取出来的字符串是空字符串（将空字符串赋值给当前sds字符串）；否则截取的字符串大小为(end-start) + 1
            newlen = (start > end) ? 0 : (end - start) + 1;
        }
    } else {    // 如果截取的字符串大小为0，设置start = 0
        start = 0;
    }

    // start = 0，表示从sds字符串开头开始截取，即不需要移动字符串位置，直接设置sds的len为截取的长度即可
    // newlen = 0,表明截取的字符串长度为0,即截取出来的字符串是空字符串，直接设置结束标记即可
    if (start && newlen) memmove(s, s + start, newlen);
    s[newlen] = '\0';
    sdssetlen(s, newlen);
}