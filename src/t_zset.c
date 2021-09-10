//
// Created by xyzjiao on 9/9/21.
//

#include "server.h"
#include "zmalloc.h"

// 创建zsl节点
zskiplistNode* zslCreateNode(int level, double score, sds ele) {
    // 注意还要为level数组申请内存
    zskiplistNode* zn = zmalloc(sizeof(*zn) + level*sizeof(struct zskiplistLevel));

    zn->score = score;
    zn->ele = ele;
    return zn;
}

// 创建zsl
zskiplist* zslCreate(void) {
    int j;
    zskiplist* zsl;

    zsl = zmalloc(sizeof(*zsl));
    // 初始化时zsl最大层数为1
    zsl->level = 1;
    zsl->length = 0;
    // 创建头节点，最大层数，不存储实际元素
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
    for (j = 0; j < ZSKIPLIST_MAXLEVEL; j++) {
        // 初始化时，节点中每个level中的前向指针初始化为NULL
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    // 头节点的后向指针初始化为NULL
    zsl->header->backward = NULL;
    // 尾节点，初始化为空，不存储实际元素
    zsl->tail = NULL;
    return zsl;
}

// 释放zsl节点
void zslFreeNode(zskiplistNode* node) {
    sdsfree(node->ele);
    zfree(node);
}

// 释放zsl
void zslFree(zskiplist* zsl) {
    // 遍历zsl第一层即可，因为第一层链接了zsl中所有节点
    zskiplistNode* node = zsl->header->level[0].forward, *next;
    // 释放头节点
    zfree(zsl->header);
    // 在zsl第一层中遍历所有节点，通过前向指针
    while (node) {
        next = node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }
    zfree(zsl);
}

int zslRandomLevel(void) {
    int level = 1;
    while ((random() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;
    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

// 向zsl中插入元素
zskiplistNode* zslInsert(zskiplist* zsl, double score, sds ele) {
    zskiplistNode* update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    x = zsl->header;
    // 从zsl的最高层开始往后遍历
    for (i = zsl->level-1; i >= 0; i--) {
        // 存储每一层查找到下探节点之前经历的跨度，当前层的跨度应该等于上一层已经走过的跨度 加上 从当前层下探节点查找到下一个探节点之间的跨度
        rank[i] = (i == (zsl->level - 1)) ? 0 : rank[i+1];

        // 寻找插入位置，查找的节点大于当前节点并且是最接近的，插入位置应该在当前节点之后
        // score大于该节点的score，或者 score等于该节点的score并且ele小于该节点的ele，就继续往后查找
        while (x->level[i].forward &&
                (x->level[i].forward->score < score ||
                    (x->level[i].forward->score == score &&
                        sdscmp(x->level[i].forward->ele, ele) < 0))) {
            // 当前层查找时累加跨度
            rank[i] += x->level[i].span;

            // 当前层往后查找
            x = x->level[i].forward;
        }
        // 当前层开始下探的那个节点存储在update中，即该节点是小于且最接近于要查找的那个节点的，插入位置在当前层的该节点之后，
        // 下一轮循环从该节点下探到下一层继续查找，直到查找到第一层，即查找节点最终要插入的问题
        update[i] = x;
    }

    /**
     * 跳表在创建节点时，随机生产每个节点的层数。此时，相邻两层链表上的节点数并不需要维持在严格的2:1关系。
     * 这样一来，当新插入一个节点时，只需要修改前后节点的指针，而其他节点的层数就不需要随之改变了，这就降低了插入操作的复杂度。
     *
     * 在Redis源码中，跳表节点层数是由zslRandomLevel函数决定。zslRandomLevel函数会将层数初始化为1，这也是节点的最小层数。
     * 然后，该函数会生成随机数，如果随机数值小于ZSKIPLIST_IP（指跳表节点增加层数的概率，值为0.25），那么层数就增加1层。因为随机数取值到[0, 0.25)范围内的概率不超过25%，
     * 所以这也就表明了，没增加一层的概率不超过25%。
     * */
    level = zslRandomLevel();

    if (level > zsl->level) {

    }
    x = zslCreateNode(level, score, ele);
}

