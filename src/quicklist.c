//
// Created by xyzjiao on 9/12/21.
//

#include "quicklist.h"
#include "zmalloc.h"

#ifndef REDIS_STATIC
#define REDIS_STATIC static
#endif



#define sizeMeetsSafetyLimit(sz) ((sz) <= SIZE_SAFETY_LIMIT)

quicklist* quicklistCreate(void) {
    struct quicklist* quicklist;

    quicklist = zmalloc(sizeof(*quicklist));
    quicklist->head = quicklist->tail = NULL;
    quicklist->len = 0;
    quicklist->count = 0;
    quicklist->compress = 0;
    quicklist->fill = -2;
    return quicklist;
}

#define COMPRESS_MAX (1<<16)
// 压缩等级
void quicklistSetCompressDepth(quicklist* quicklist, int compress) {
    if (compress > COMPRESS_MAX)
        compress = COMPRESS_MAX;
    else if(compress < 0)
        compress = 0;
    quicklist->compress = compress;
}

#define FILL_MAX (1<<15)
void quicklistSetFill(quicklist* quicklist, int fill) {
    if (fill > FILL_MAX)
        fill = FILL_MAX;
    else if (fill < -5)
        fill = -5;
    quicklist->fill = fill;
}

void quicklistSetOptions(quicklist* quicklist, int fill, int depth) {
    quicklistSetFill(quicklist, fill);
    quicklistSetCompressDepth(quicklist, depth);
}

quicklist* quciklistNew(int fill, int compress) {
    quicklist* quicklist = quicklistCreate();
    quicklistSetOptions(quicklist, fill, compress);
    return quicklist;
}

REDIS_STATIC quicklistNode* quicklistCreateNode(void) {
    quicklistNode* node;
    node = zmalloc(sizeof(*node));
    node->zl = NULL;
    node->count = 0;
    node->sz = 0;
    node->next = node->prev = NULL;
    node->encoding = QUCIKLIST_NODE_ENCODING_RAW;
    node->container = QUCIKLIST_NODE_CONITAINER_ZIPLIST;
    node->recompress = 0;
    return node;
}

unsigned long quicklistCount(const quicklist* ql) { return ql->count; }

void quicklistRelease(quicklist* quicklist) {
    unsigned long len;
    quicklistNode* current, *next;

    current = quicklist->head;
    len = quicklist->len;
    while (len--) {
        next = current->next;

        zfree(current->zl);
        quicklist->count -= current->count;

        zfree(current);

        quicklist->len--;
        current = next;
    }
    zfree(quicklist);
}


REDIS_STATIC void __quicklistInsertNode(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node, int after) {
    // 在给定的node(old_node)之后插入
    if (after) {
        new_node->prev = old_node;
        if (old_node) {
            new_node->next = old_node->next;
            if (old_node->next)
                old_node->next->prev = new_node;
            old_node->next = new_node;
        }
        if (quicklist->tail == old_node)
            quicklist->tail = new_node;
    } else {    // 之前插入
        new_node->next = old_node;
        if (old_node) {
            new_node->prev = old_node->prev;
            if (old_node->prev)
                old_node->prev->next = new_node;
            old_node->prev = new_node;
        }
        if (quicklist->head == old_node)
            quicklist->head = new_node;
    }

    if (quicklist->len == 0)
        quicklist->head = quicklist->tail = new_node;

    quicklist->len++;
}

REDIS_STATIC void __quicklistInsertNodeBefore(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node) {
    __quicklistInsertNode(quicklist, old_node, new_node, 0);
}

REDIS_STATIC void __quicklistInsertNodeAfter(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node) {
    __quicklistInsertNode(quicklist, old_node, new_node, 1);
}

// sz是当前node，新增元素之后，ziplist的大小
REDIS_STATIC int _quicklistNodeSizeMeetsOptimizationRequirement(const size_t sz, const int fill) {
    if (fill >= 0)
        return 0;

    size_t offset = (-fill) - 1;
    if (offset < sizeof(optimization_level) / sizeof(*optimization_level)) { // 计算optimization_level数组大小
        if (sz <= optimization_level[offset])   // 如果该node新增元素后的ziplist大小没有超过了quicklist预定的每个node的ziplist的最大大小，就直接在该node的ziplist中新增元素
            return 1;
        else
            return 0;
    } else {
        return 0;
    }
}

// 判断插入元素时，是插入到当前node的ziplist中，还是新建一个node，插入到新的node的ziplist中
// 从而限制一个node的ziplist的大小
REDIS_STATIC int __quicklistNodeAllowInsert(const quicklistNode* node, const int fill, const size_t sz) {
    if (node == NULL)
        return 0;

    int ziplist_overhead = 0;   // 插入元素之后，ziplist需要扩容的大小，不考虑连锁更新

    // 计算prevlensize
    if (sz < 254) // 插入元素的长度小于254，prevlensize为1个字节
        ziplist_overhead = 1;
    else // 插入元素的长度大于等于254，prevlensize为5个字节
        ziplist_overhead = 5;

    // 计算encoding的长度
    if (sz < 64)    // 长度小于等于63字节的字节数组，encoding为1个字节
        ziplist_overhead += 1;
    else if (sz < 16384)    // 长度小于等于16 383字节的字节数组，encoding为2个字节
        ziplist_overhead += 2;
    else
        ziplist_overhead += 5;

    // 该quicklist节点对应的ziplist新的总长度（prevlensize + encodingsize + size）
    unsigned int new_sz = node->sz + sz + ziplist_overhead;


    if(_quicklistNodeSizeMeetsOptimizationRequirement(new_sz, fill)) // 判断该node新增元素后的ziplist的大小，是否大于quicklist预定的每个node的ziplist的最大大小，从而限制该node的ziplist的大小
        return 1;
    else if (!sizeMeetsSafetyLimit(new_sz))
        return 0;
    else if ((int) node->count < fill)  // 限制该node的ziplist中的元素个数
        return 1;
    else
        return 0;
}


