//
// Created by xyzjiao on 9/9/21.
//

#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H
#include "sds.h"

// 可以容纳2^64个元素
#define ZSKIPLIST_MAXLEVEL 64

#define ZSKIPLIST_P 0.25

typedef struct zskiplistNode {
    sds ele;    // 存储元素
    double score;   // 元素分值
    struct zskiplistNode* backward; // 后向指针(指向前一个节点，相邻的前面一个节点)
    struct zskiplistLevel {
        struct zskiplistNode* forward; // 指向后面的节点（不一定是相邻的后面一个节点，可能有跨度）
        unsigned long span; // 本节点  到forward指针指向的后面节点 的跨度
    } level[];  // 节点的level数组，保存每层上的前向指针(指向后一个节点)和跨度
} zskiplistNode;

typedef struct zskiplist {
    struct zskiplistNode* header, *tail;    // 指向跳表的表头和表尾节点
    unsigned long length;   // 跳表的长度，即跳表包含的节点数量（表头节点不计算在内）
    int level;  // 跳表最大层数
} zskiplist;

zskiplistNode* zslCreateNode(int level, double score, sds ele);
zskiplist* zslCreate(void);
void zslFreeNode(zskiplistNode* node);
void zslFree(zskiplist* zsl);
zskiplistNode* zslInsert(zskiplist* zsl, double score, sds ele);



#endif //REDIS_SERVER_H
