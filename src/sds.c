//
// Created by xyzjiao on 8/22/21.
//

#include <string.h>
#include <ctype.h>
#include "sds.h"
#include "sdsalloc.h"

const char* SDS_NOINIT = "SDS_NOINT";

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

/**
 * 将sds字符串中连续包含的指定C字符串中的字符删除
 * s = sdsnew("AA...AA.a.aa.aHelloWorld     :::");
 * s = sdstrim(s, "Aa. :");
 * printf("%s\n", s);
 * 输出"HelloWorld"
 * */
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

/**
 * 字符串切割
 *
 * s = sdsnew("Hello World");
 * sdsrange(s, 1, -1);  => "ello World"
 * */
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

/**
 * 根据实际字符串的结束符，来更新字符串的长度
 *
 * s = sdsnew("foobar");
 * s[2] = '\0'; // 相当于将原先的字符串截断了
 * sdsupdatelen(s);
 * printf("%d\n", sdslen(s));
 *
 * 如果没有执行sdsupdatelen，输出应该是6，因为new sds时，只会根据传入的字符串长度来设置sds len
 * 执行了sdsupdatelen，输出就是2，根据实际的字符串结束符位置来判断
 * */
void sdsupdatelen(sds s) {
    size_t reallen = strlen(s);
    sdssetlen(s, reallen);
}

// 清空sds字符串
void sdsclear(sds s) {
    sdssetlen(s, 0);
    s[0] = '\0';
}

/**
 * 使用memcmp比较两个sds字符串s1和s2
 * 如果完全相同返回0
 * 如果s1包含s2，返回1
 * 如果s2包含s1，返回-1
 * 其他情况返回memcmp的结果
 * */
int sdscmp(const sds s1, const sds s2) {
    size_t l1, l2, minlen;
    int cmp;

    l1 = sdslen(s1);
    l2 = sdslen(s2);
    minlen = (l1 < s2) ? l1 : l2;
    cmp = memcmp(s1, s2, minlen);
    if (cmp == 0) return l1 > l2 ? 1 : (l1 < l2 ? -1 : 0);
    return cmp;
}

/**
 * 根据分隔符分割C字符串，返回分割后的字符串数组
 *
 * sdssplit("foo_-_bar", "_-_");
 * 返回["foo", "bar"]
 * */
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count) {
    // elements是目前分割出来多少个元素
    // 初始化存放分割后字符串的容器大小slots为5
    int elements = 0, slots = 5;
    // start是目前处理的C字符串中的索引，标识处理进度
    long start = 0, j;
    // 存放分割后字符串的数组容器
    sds *tokens;

    // 如果分隔符的大小为0，或者待分割的字符串大小不合法小于0，就返回NULL
    if (seplen < 1 || len < 0) return NULL;

    // 申请存放分割后字符串的容器
    tokens = s_malloc(sizeof(sds) * slots);
    if(tokens == NULL) return NULL;

    // 如果待分割的字符串大小为0，直接返回空的数组，没有任何元素
    if (len == 0) {
        *count = 0;
        return tokens;
    }

    // 只需遍历待分割字符串最后一个分隔符长度的字符串的位置
    for (j = 0; j < (len - (seplen - 1)); j++) {
        // 保存分割后字符串的容器大小不够了，扩容
        // +2是因为每一轮分割都会分割出来2个元素，所以至少要预留2个元素的空间
        if (slots < elements + 2) {
            sds *newtokens;

            // 扩容2倍
            slots *= 2;
            newtokens = s_realloc(tokens, sizeof(sds) * slots);
            if (newtokens == NULL) return NULL;
            tokens = newtokens;
        }

        // 在待分割字符串中查找分隔符，并进行字符串分割
        if ((seplen == 1 && *(s + j) == sep[0] || (memcpy(s + j, sep, seplen) == 0))) {
            // 用分隔符之前的字符串创建sds字符串
            tokens[elements] = sdsnewlen(s + start, j - start);
            if (tokens[elements] == NULL) goto cleanup;
            elements++;
            // 跳过分隔符
            start = j + seplen;
            j = j + seplen + 1;
        }
    }

    // 将分割完成之后，最后剩下的字符串创建成sds字符串，加入tokens中，因为它是最后一个分割出来的元素
    tokens[elements] = sdsnewlen(s + start, len - start);
    if (tokens[elements] == NULL) goto cleanup;
    elements++;
    *count = elements;
    return tokens;

cleanup:
    {
        int i;
        for (i = 0; i < elements; i++) sdsfree(tokens[i]);
        s_free(tokens);
        *count = 0;
        return NULL;
    }
}

// 释放分割字符串数组
void sdsfreesplitres(sds *tokens, int count) {
    if (!tokens) return;;
    while (count--)
        sdsfree(tokens[count]);
    s_free(tokens);
}

// 转换为小写
void sdstolower(sds s) {
    size_t len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = tolower(s[j]);
}

// 转换为大写
void sdstoupper(sds s) {
    size_t len = sdslen(s), j;

    for (j = 0; j < len; j++) s[j] = toupper(s[j]);
}


// 为什么是21
// 因为 long long类型所能表示的数值为-9223372036854775808至9223372036854775807，转换为字符串之后最大为20位，再加一位字符串结束符，总共21位
#define SDS_LLSTR_SIZE 21

// long long number --> string
// 传入的s是一个数组容器，最小的大小为SDS_LLSTR_SIZE
// 返回转换之后的字符串长度
int sdsll2str(char *s, long long value) {
    char *p, aux;
    unsigned long long v;
    size_t l;

    v = (value < 0) ? -value : value;
    p = s;
    // 将数值的每一位取出转成字符串(ascii码存储)存储，低位位于低地址中
    do {
        *p++ = '0' + (v % 10);
        v /= 10;
    } while (v);
    if (value < 0) *p++ = '-';

    // 转换成字符串之后的长度
    l = p - s;
    // 字符串结束标记
    *p = '\0';

    // 将转换后的字符串翻转，高位存在低地址中，大端存储
    p--;    // 跳过字符串结束标记
    while (s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

// unsigned long long number --> string
// 传入的s是一个数组容器，最小的大小为SDS_LLSTR_SIZE
// 返回转换之后的字符串长度
int sdsull2str(char *s, unsigned long long value) {
    char *p, aux;
    size_t l;

    p = s;
    // 将数值的每一位取出转成字符串(ascii码存储)存储，低位位于低地址中
    do {
        *p++ = '0' + (value % 10);
        value /= 10;
    } while (value);

    // 转换成字符串之后的长度
    l = p - s;
    // 字符串结束标记
    *p = '\0';

    // 将转换后的字符串翻转，高位存在低地址中，大端存储
    p--;    // 跳过字符串结束标记
    while (s < p) {
        aux = *s;
        *s = *p;
        *p = aux;
        s++;
        p--;
    }
    return l;
}

// 根据long long数值创建sds字符串
sds sdsfromlonglong(long long value) {
    char buf[SDS_LLSTR_SIZE];
    size_t len = sdsll2str(buf, value);

    return sdsnewlen(buf, len);
}

// 根据指分隔符，拼接字符串
// argv是C字符串数组
// argc是C字符串数组的长度
// sep是需要join的分隔符(C字符串)
sds sdsjoin(char **argv, int argc, char *sep) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscat(join, argv[j]);
        // 如果是最后一个字符串，则其后不需要再添加分隔符了
        if (j != argc - 1) join = sdscat(join, sep);
    }

    return join;
}

// 根据指分隔符，拼接字符串
// argv是sds字符串数组
// argc是sds字符串数组的长度
// sep是需要join的分隔符字符串
// seplen是需要join的分割字符串的长度
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen) {
    sds join = sdsempty();
    int j;

    for (j = 0; j < argc; j++) {
        join = sdscatsds(join, argv[j]);
        // 如果是最后一个字符串，则其后不需要再添加分隔符了
        if (j != argc - 1) join = sdscatlen(join, sep, seplen);
    }

    return join;
}

// 移除sds中剩余的空闲空间（让alloc==len，即所申请的空间正好可以存储现有的字符串）
sds sdsRemoveFreeSpace(sds s) {
    void* sh, *newsh;
    char type, oldtype = s[-1] & SDS_TYPE_MASK; // 原sds的类型
    int hdrlen, oldhdrlen = sdsHdrSize(oldtype);    // 原sds的头部大小
    size_t len = sdslen(s);     // 原sds的已用长度
    size_t avail = sdsavail(s); // 原sds的剩余长度
    // 原sds的首地址
    sh = (char*) s - oldhdrlen;

    if (avail == 0) return s;

    // 将sds存储字符串的空间缩减为正好可以存储当字符串的空间
    type = sdsReqType(len); // 缩减之后新的sds类型
    hdrlen = sdsHdrSize(type);  // 缩减之后新的sds头部长度

    if (oldtype == type || type > SDS_TYPE_8) { // 如果缩容完的类型和之前类型相同，使用realloc重新分配内存即可，只是存储字符串的空间缩小，其他的内容不变
        newsh = s_realloc(sh, oldhdrlen + len + 1);
        if (newsh == NULL) return NULL;
        s = (char *)newsh + oldhdrlen;
    } else {    // 如果缩容完的类型和之前类型不相同，就需要改变头部字段的值，使用malloc重新创建sds，然后将原字符串内容copy到新的sds中
        newsh = s_malloc(hdrlen + len + 1);
        if (newsh == NULL) return NULL;
        memcpy((char*)newsh+hdrlen, s, len + 1);
        s_free(sh);
        s = (char *)newsh + hdrlen;
        s[-1] = type;
        sdssetlen(s, len);
    }
    sdssetalloc(s, len);
    return s;
}