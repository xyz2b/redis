//
// Created by xyzjiao on 9/9/21.
//

#include "server.h"

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

// 向zsl中插入元素，返回插入的节点
zskiplistNode* zslInsert(zskiplist* zsl, double score, sds ele) {
    zskiplistNode* update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;

    x = zsl->header;
    // 从zsl的最高层开始往后遍历
    for (i = zsl->level-1; i >= 0; i--) {
        // 存储每一层查找到下探节点之前经历的跨度，当前层的跨度应该等于上一层已经走过的跨度 加上 从当前层下探节点查找到下一个探节点之间的跨度
        rank[i] = (i == (zsl->level - 1)) ? 0 : rank[i+1];

        // 寻找插入位置，要查找的节点 应该要 大于当前节点 并且 小于当前节点的下一个节点
        // 当前遍历节点的score的下一个节点的score大于要查找节点的score，
        //      或者当前遍历节点的下一个节点的score等于要查找节点的score，但是其ele大于要查找的ele，就表明当前节点就是下探节点，从当前遍历节点的当前层往下一层继续查找
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
     * 所以这也就表明了，每增加一层的概率不超过25%。
     * */
     // 随机返回插入节点的层数
    level = zslRandomLevel();

    // 增加层数
    // 初始化新增的层，新增层的下探节点是header，其后面的节点为尾节点，跨度为zsl中所有元素的个数
    if (level > zsl->level) {
        for (i = zsl->level; i < level; i ++) {
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }
        zsl->level = level;
    }

    x = zslCreateNode(level, score, ele);

    // 处理插入节点所包含的层数（插入节点的层数可能大于当前最大层数，也可能小于等于当前最大层数）
    // 在层中插入新增的节点，插入的节点在当前层的下探节点之后
    for (i = 0; i < level; i++) {
        // 构建新插入节点的每一层前向指针，指向本层插入点之前的节点(查找时的下探节点)的前向指针所指向的内容
        x->level[i].forward = update[i]->level[i].forward;
        // 修改本层插入点之前的节点(查找时的下探节点)的前向指针 指向 插入的节点
        update[i]->level[i].forward = x;

        // 当前层插入节点和后面一个节点的跨度 = 插入前下探节点和后面一个节点的跨度 - (插入节点和下探节点之间的跨度)
        // rank存储的是当前层查找到下探节点经过的跨度
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
        // 更新下探节点在当前层的跨度（插入节点和下探节点之间的跨度)
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }

    // 插入节点的层数小于当前最大的层数，那么上面的循环只能处理插入节点层数，还剩下的那部分就需要下面的处理，
    // 只需要将下探节点的当前层跨度加1即可，因为插入节点在下探节点之后，所以下探节点之后会多一个节点，同时插入节点又没有这些层，所以下探节点的跨度加一
    for (int i = level; i < zsl->level; i++) {
        update[i]->level[i].span++;
    }

    // update存储的是下探节点，则第一层的下探节点，就应该是插入节点前面那个节点
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if (x->level[0].forward) {
        x->level[0].forward->backward = x;
    } else {    // 如果x->level[0].forward为空，说明x是zsl中最后一个节点，更新zsl tail指针
        zsl->tail = x;
    }
    zsl->length++;
    return x;
}

// 删除一个节点，但并不释放该节点，只是从链中移除，所以记得不用时释放节点
// update是一个数组，数组元素是zskiplistNode*，其中存储在查找x过程中每一层的下探节点
void zslDeleteNode(zskiplist* zsl, zskiplistNode* x, zskiplistNode** update) {
    int i;

    for (i = 0; i < zsl->level; i++) {
        // 如果每一层下探节点的后一个节点是x，就需要更新forward指针
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        } else {    // 如果每一层下探节点的后一个节点不是x，那就跨度减一，因为要删除其后面的一个节点，并且这个节点不在该层
            update[i]->level[i].span -= 1;
        }
    }

    // 更新backward
    if (x->level[0].forward) {
        x->level[0].forward->backward = x->backward;
    } else {
        zsl->tail = x->backward;
    }

    // 更新zsl的最大层数，从当前最大层开始往下可能连续几层只有被删除的节点，所以需要循环处理
    while (zsl->level > 1 && zsl->header->level[zsl->level - 1].forward == NULL) {
        zsl->level--;
    }
    zsl->length--;
}

// 删除匹配的元素，同时返回被删除的节点
int zslDelete(zskiplist* zsl, double score, sds ele, zskiplistNode** node) {
    zskiplistNode* update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    // 查找节点，并记录每一层的下探节点
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score &&
                 sdscmp(x->level[i].forward->ele, ele) < 0))) {
            // 当前层往后查找
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // 取第一层下探节点后面一个节点，如果该节点元素和给定的元素不等，就是没找到匹配的节点
    x = x->level[0].forward;
    if (x && score == x->score && sdscmp(x->ele, ele) == 0) {
        zslDeleteNode(zsl, x, update);
        if (!node)
            zslFreeNode(x);
        else
            *node = x;
        return 1;
    }

    return 0;
}

// 更新指定元素指定分值对应节点的分值
zskiplistNode* zslUpdateScore(zskiplist* zsl, double curscore, sds ele, double newscore) {
    zskiplistNode* update[ZSKIPLIST_MAXLEVEL], *x;
    int i;

    x = zsl->header;
    // 查找节点，并记录每一层的下探节点
    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < curscore ||
                (x->level[i].forward->score == curscore &&
                 sdscmp(x->level[i].forward->ele, ele) < 0))) {
            // 当前层往后查找
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    // 取第一层下探节点后面一个节点，如果该节点元素和给定的元素不等，就是没找到匹配的节点
    x = x->level[0].forward;
    // 没有找到匹配的节点
    assert(x && curscore == x->score && sdscmp(x->ele, ele) == 0);

    // 因为跳表是有序的，所以需要维护修改过分值的节点顺序
    // 如果修改过分值之后它在跳表的位置没有发生变化，还是比前面一个节点分值大，比后面一个节点分值小，就直接更新分值即可
    if ((x->backward == NULL || x->backward->score < newscore) && (x->level[0].forward == NULL || x->level[0].forward->score > newscore)) {
        x->score = newscore;
        return x;
    }

    // 如果修改过分值之后，该节点的位置发生了变化，就从zsl中删除该节点，重新插入，因为涉及删除所以上面遍历了所有层
    zslDeleteNode(zsl, x, update);
    // 这里复用了被修改节点的ele
    zskiplistNode* newnode = zslInsert(zsl, newscore, x->ele);
    x->ele = NULL;
    zslFreeNode(x);
    return newnode;
}

// 查找节点，并返回查找到节点经过的跨度，找不到返回0
unsigned long zslGetRank(zskiplist* zsl, double score, sds ele) {
    zskiplistNode* x;
    unsigned long rank = 0;
    int i;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score &&
                 sdscmp(x->level[i].forward->ele, ele) <= 0))) {
            // 这里循环条件变成了等于0，标识循环结束之后的节点即是与查找分值相同的节点，不需要再获取循环结束节点的下一个节点了。
            // 因为我们只需要找到匹配的节点即可，不需要遍历所有的层，在某一层找到对应的节点直接返回即可

            // 累加跨度
            rank += x->level[i].span;
            // 当前层往后查找
            x = x->level[i].forward;
        }

        // 比较ele是否相等
        if (x->ele && sdscmp(x->ele, ele) == 0) {
            return rank;
        }
    }

    return 0;
}

// 根据跨度，查找节点
zskiplistNode* zslGetElementByRank(zskiplist* zsl, unsigned long rank) {
    zskiplistNode* x;
    unsigned long traversed = 0;    // 表示已经经过的跨度
    int i;

    x = zsl->header;
    for (i = zsl->level-1; i >= 0; i--) {

        while (x->level[i].forward && (traversed + x->level[i].span) <= rank) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
        if (traversed == rank)
            return x;
    }

    return NULL;
}


// 判断value是否大于(或等于)区间的最小值
int zslValueGteMin(double value, zrangespec* spec) {
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

// 判断value是否小于(或等于)区间的最大值
int zslValueLteMax(double value, zrangespec* spec) {
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

// 判断区间是否在zsl中
int zslIsInRange(zskiplist* zsl, zrangespec* range) {
    zskiplistNode* x;

    // 区间为空
    if (range->min > range->max || (range->min == range->max && (range->minex || range->maxex)))
        return 0;


    x = zsl->tail;
    // zsl中最大的比区间最小的还要小，表明range不在zsl中
    if (x == NULL || !zslValueGteMin(x->score, range))
        return 0;

    x = zsl->header->level[0].forward;
    // zsl中最小的比区间最大的还要大，表明range不在zsl中
    if (x == NULL || !zslValueLteMax(x->score, range))
        return 0;

    return 1;
}

// 在zsl中查找分值在区间中的第一个节点
zskiplistNode* zslFirstInRange(zskiplist* zsl, zrangespec* range) {
    zskiplistNode* x;
    int i;

    if (!zslIsInRange(zsl, range)) return NULL;

    x= zsl->header;
    for (i = zsl->level-1; i >=0; i--) {
        // 当前遍历节点的下一个节点分值在区间内，就跳出循环，表明当前遍历的节点下一个节点就是第一个在区间内的节点
        while (x->level[i].forward && !zslValueGteMin(x->level[i].forward->score, range))
            x = x->level[i].forward;
    }

    // 获取当前遍历节点的下一个节点
    x = x->level[0].forward;
    assert(x != NULL);

    // 如果找到节点分值已经超出了区间最大值，那就是不在区间内
    if (!zslValueLteMax(x->score, range)) return NULL;
    return x;
}

// 在zsl中查找分值在区间中的最后一个节点
zskiplistNode* zslLastInRange(zskiplist* zsl, zrangespec* range) {
    zskiplistNode* x;
    int i;

    if (!zslIsInRange(zsl, range)) return NULL;

    x= zsl->header;
    for (i = zsl->level-1; i >=0; i--) {
        // 当前遍历节点的下一个节点分值不在区间内，就跳出循环，表明当前遍历的节点就是最后一个在区间内的节点
        while (x->level[i].forward && zslValueLteMax(x->level[i].forward->score, range))
            x = x->level[i].forward;
    }

    assert(x != NULL);

    // 如果找到的节点分值已经低于区间最小值，那就是不在区间内
    if (!zslValueGteMin(x->score, range)) return NULL;
    return x;
}