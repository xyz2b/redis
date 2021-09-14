//
// Created by xyzjiao on 9/12/21.
//

#include <string.h>
#include "quicklist.h"
#include "zmalloc.h"
#include "ziplist.h"
#include "util.h"

#ifndef REDIS_STATIC
#define REDIS_STATIC static
#endif



#define sizeMeetsSafetyLimit(sz) ((sz) <= SIZE_SAFETY_LIMIT)

// 初始化entry
#define initEntry(e) \
    do {             \
        (e)->zi = (e)->value = NULL;                 \
        (e)->longval = -123456789;                   \
        (e)->quicklist = NULL;                       \
        (e)->node = NULL;                            \
        (e)->offset = 123456789;                     \
        (e)->sz = 0;\
    } while(0)


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

quicklist* quicklistNew(int fill, int compress) {
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


REDIS_STATIC void _quicklistInsertNode(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node, int after) {
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

REDIS_STATIC void _quicklistInsertNodeBefore(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node) {
    _quicklistInsertNode(quicklist, old_node, new_node, 0);
}

REDIS_STATIC void _quicklistInsertNodeAfter(quicklist* quicklist, quicklistNode* old_node, quicklistNode* new_node) {
    _quicklistInsertNode(quicklist, old_node, new_node, 1);
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
REDIS_STATIC int _quicklistNodeAllowInsert(const quicklistNode* node, const int fill, const size_t sz) {
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

REDIS_STATIC int _quicklistNodeAllowMerge(const quicklistNode* a, const quicklistNode* b, const int fill) {
    if (!a || !b)
        return 0;

    // -11是因为两个ziplist合并了，就会少一个头和一个尾，总共11字节
    unsigned int merge_sz = a->sz + b->sz - 11;
    if(_quicklistNodeSizeMeetsOptimizationRequirement(merge_sz, fill))
        return 1;
    else if (!sizeMeetsSafetyLimit(merge_sz))
        return 0;
    else if ((int)(a->count + b->count) < fill)
        return 1;
    else
        return 0;
}

// 更新节点对应的ziplist的长度
#define quicklistNodeUpdateSz(node) \
    do {                            \
        (node)->sz = ziplistBlobLen((node->zl));                                \
    } while(0)


// 在quicklist头部插入元素
// 返回0，表示没有新创建node，是在现有quicklist头部node的ziplist中直接插入元素的
// 返回1，表示新创建了头部node，在新创建的头部node的ziplist中插入元素的
int quicklistPushHead(quicklist* quicklist, void* value, size_t sz) {
    quicklistNode* orig_head = quicklist->head;
    if (_quicklistNodeAllowInsert(quicklist->head, quicklist->fill, sz)) { // 可以在本节点的ziplist中直接插入
        quicklist->head->zl = ziplistPush(quicklist->head->zl, value, sz, ZIPLIST_HEAD);
        quicklistNodeUpdateSz(quicklist->head);
    } else {    // 新建一个节点插入
        // 新建节点
        quicklistNode* node = quicklistCreateNode();
        // 新建节点的ziplist，并将新元素插入到其中
        node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);
        quicklistNodeUpdateSz(node);
        // 将新节点插入到当前quicklist头部之前
        _quicklistInsertNodeBefore(quicklist, quicklist->head, node);
    }
    quicklist->count++;
    quicklist->head->count++;
    return (orig_head != quicklist->head);
}

// 在quicklist尾部插入元素
// 返回0，表示没有新创建node，是在现有quicklist尾部node的ziplist中直接插入元素的
// 返回1，表示新创建了尾部node，在新创建的尾部node的ziplist中插入元素的
int quicklistPushTail(quicklist* quicklist, void* value, size_t sz) {
    quicklistNode* orig_tail = quicklist->tail;
    if (_quicklistNodeAllowInsert(quicklist->tail, quicklist->fill, sz)) { // 可以在本节点的ziplist中直接插入
        quicklist->tail->zl = ziplistPush(quicklist->tail->zl, value, sz, ZIPLIST_TAIL);
        quicklistNodeUpdateSz(quicklist->tail);
    } else {    // 新建一个节点插入
        // 新建节点
        quicklistNode* node = quicklistCreateNode();
        // 新建节点的ziplist，并将新元素插入到其中
        node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_TAIL);
        quicklistNodeUpdateSz(node);
        // 将新节点插入到当前quicklist尾部之后
        _quicklistInsertNodeAfter(quicklist, quicklist->tail, node);
    }
    quicklist->count++;
    quicklist->tail->count++;
    return (orig_tail != quicklist->tail);
}

// 在quicklist尾部新增一个节点，节点的ziplist是传入的zl
void quicklistAppendZiplist(quicklist* quicklist, unsigned char* zl) {
    quicklistNode* node = quicklistCreateNode();

    node->zl = zl;
    node->count = ziplistLen(node->zl);
    node->sz = ziplistBlobLen(node->zl);

    _quicklistInsertNodeAfter(quicklist, quicklist->tail, node);
    quicklist->count += node->count;
}

// 将ziplist中的所有元素插入到quicklist中
quicklist* quicklistAppendValuesFromZiplist(quicklist* quicklist, unsigned char* zl) {
    unsigned char* value;
    unsigned int sz;
    long long longval;
    char longstr[32] = {0};

    // 获取ziplist第一项
    unsigned char* p = ziplistIndex(zl, 0);
    // 循环获取ziplist中每一项的值
    while (ziplistGet(p, &value, &sz, &longval)) {
        // value为空，说明存储的元素是数值，不是字符串
        if (!value) {
            // 将long long整型数值转成字符串
            sz = ll2string(longstr, sizeof(longstr), longval);
            value = (unsigned char*)longstr;
        }
        quicklistPushTail(quicklist, value, sz);
        p = ziplistNext(zl, p);
    }
    zfree(zl);
    return quicklist;
}


// 根据ziplist的元素创建新的quicklist
quicklist* quicklistCreateFromZiplist(int fill, int compress, unsigned char* zl) {
    return quicklistAppendValuesFromZiplist(quicklistNew(fill, compress), zl);
}

#define quicklistDeleteIfEmpty(ql, n) \
    do {                              \
        if ((n)->count == 0) {        \
            _quicklistDelNode((ql), (n)); \
            (n) = NULL;\
        }\
    } while(0)

// 删除quicklist node
REDIS_STATIC void _quicklistDelNode(quicklist* quicklist, quicklistNode* node) {
    if (node->next)
        node->next->prev = node->prev;
    if (node->prev)
        node->prev->next = node->next;

    if (node == quicklist->tail)
        quicklist->tail = node->prev;

    if (node == quicklist->head)
        quicklist->head = node->next;

    // __quicklistCompress(quicklist, NULL);

    quicklist->count -= node->count;

    zfree(node->zl);
    zfree(node);
    quicklist->len--;
}

// p指向给定的node的ziplist中需要被删除的项
// 返回1表示该node被删除了，即删除的元素是该node中最后一个元素，删除后该node为空；返回0表示删除该元素之后该node还没有为空
REDIS_STATIC int quicklistDelIndex(quicklist* quicklist, quicklistNode* node, unsigned char** p) {
    int gone = 0;

    // 删除ziplist中p指向的项
    node->zl = ziplistDelete(node->zl, p);
    node->count--;

    if (node->count == 0) {
        gone = 1;
        _quicklistDelNode(quicklist, node);
    } else {
        quicklistNodeUpdateSz(node);
    }
    quicklist->count--;
    return gone ? 1 : 0;
}

// 从quicklist中删除一个元素
void quicklistDelEntry(quicklistIter* iter, quicklistEntry* entry) {
    quicklistNode* prev = entry->node->prev;
    quicklistNode* next = entry->node->next;
    int delete_node = quicklistDelIndex((quicklist*)entry->quicklist, entry->node, &entry->zi);

    iter->zi = NULL;

    if (delete_node) {  // 如果档期遍历的node被删除，就需要调整迭代器的当前指向，反之不需要调整
        if (iter->direction == AL_START_HEAD) { // 从头到尾遍历
            iter->current = next;
            iter->offset = 0;
        } else if (iter->direction == AL_START_TAIL) {  // 从尾到头遍历
            iter->current = prev;
            iter->offset = -1;
        }
    }
}

// 查询quicklist指定索引处的元素，以quicklistEntry类型参数返回
// 0表示没找到，1表示找到
int quicklistIndex(const quicklist* quicklist, const long long idx, quicklistEntry* entry) {
    quicklistNode* n;
    unsigned long long accum = 0;
    unsigned long long index;
    int forward = idx < 0 ? 0 : 1;

    initEntry(entry);
    entry->quicklist = quicklist;

    if (!forward) { // idx为负，从尾到头遍历
        index = (-idx) - 1;
        n = quicklist->tail;
    } else {    // idx为正，从头到尾遍历
        index = idx;
        n = quicklist->head;
    }

    if (index >= quicklist->count)
        return 0;

    // 遍历找到要查找的index属于quicklist哪个node
    // 即当遍历的node的所有元素数量刚好大于index时，则当前遍历的node就包含了index索引的元素
    while (n) {
        if ((accum + n->count) > index) {
            break;
        } else {
            accum += n->count;
            n = forward ? n->next : n->prev;
        }
    }

    if (!n)
        return 0;

    entry->node = n;
    // 计算index索引的元素在当前node的ziplist中的偏移量
    if (forward) {
        entry->offset = index - accum;
    } else {
        // offset为负的，从尾到头
        entry->offset = (-index) - 1 + accum;
    }
    // 获取index索引的元素在ziplist中的项
    entry->zi = ziplistIndex(entry->node->zl, entry->offset);
    // 获取index索引的元素在ziplist中的值
    ziplistGet(entry->zi, &entry->value, &entry->sz, &entry->longval);
    return 1;
}

// 替换quicklist索引index处的元素值，data为新的元素值，sz为新的元素值的长度
// 0没有发生替换，1发生替换
int quicklistReplaceAtIndex(quicklist* quicklist, long index, void* data, int sz) {
    quicklistEntry entry;

    if (quicklistIndex(quicklist, index, &entry)) {
        // 替换：删掉重新插入
        entry.node->zl = ziplistDelete(entry.node->zl, &entry.zi);
        entry.node->zl = ziplistInsert(entry.node->zl, entry.zi, data, sz);
        quicklistNodeUpdateSz(entry.node);
        return 1;
    } else {
        return 0;
    }
}

// 合并两个node的ziplist，返回合并后的节点
REDIS_STATIC quicklistNode* _quicklistZiplistMerge(quicklist* quicklist, quicklistNode* a, quicklistNode* b) {
    // ziplistMerge内部会根据两个ziplist的长度来决定是将谁拼接到谁的后面，然后传入的两个参数，有个会被置为NULL，说明它被拼接到另一个后面去了
    if ((ziplistMerge(&a->zl, &b->zl))) {
        quicklistNode* keep = NULL, *nokeep = NULL; // 拼接完之后需要保留的节点，和需要释放的节点

        // a->zl为空，说明a->zl倍拼接到b->zl后面了，a节点可以释放了，b节点要保留
        if (!a->zl) {
            nokeep = a;
            keep = b;
        } else if (!b->zl) {    // b->zl为空，说明b->zl倍拼接到a->zl后面了，b节点可以释放了，a节点要保留
            nokeep = b;
            keep = a;
        }
        keep->count = ziplistLen(keep->zl);
        quicklistNodeUpdateSz(keep);

        nokeep->count = 0;
        _quicklistDelNode(quicklist, nokeep);
        return keep;
    } else {
        return NULL;
    }
}

/* Attempt to merge ziplists within two nodes on either side of 'center'.
 *
 * We attempt to merge:
 *   - (center->prev->prev, center->prev)
 *   - (center->next, center->next->next)
 *   - (center->prev, center)
 *   - (center, center->next)
 */
// 将center和其前后两个节点尝试进行合并
REDIS_STATIC void _quicklistMergeNodes(quicklist* quicklist, quicklistNode* center) {
    int fill = quicklist->fill;
    quicklistNode* prev, *prev_prev, *next, *next_next, *target;
    prev = prev_prev = next = next_next = target = NULL;

    if (center->prev) {
        prev = center->prev;
        if (center->prev->prev)
            prev_prev = center->prev->prev;
    }

    if (center->next) {
        next = center->next;
        if (center->next->next)
            next_next = center->next->next;
    }

    // try to merge prev_prev and prev
    if (_quicklistNodeAllowMerge(prev, prev_prev, fill)) {
        _quicklistZiplistMerge(quicklist, prev_prev, prev);
        prev_prev = prev = NULL;
    }

    // try to merge next_next and next
    if (_quicklistNodeAllowMerge(next, next_next, fill)) {
        _quicklistZiplistMerge(quicklist, next, next_next);
        next = next_next = NULL;
    }

    // try to merge center and center->prev
    if (_quicklistNodeAllowMerge(center, center->prev, fill)) {
        target = _quicklistZiplistMerge(quicklist, center->prev, center);   // 接收返回值，因为下面需要将它和它的下一个节点合并
        center = NULL;  // 合并完之后，center指针就没用了
    } else {
        // 不进行merge，但是需要target指针，以供下面进行merge
        target = center;
    }

    // use result of center merge to merge with next
    if (_quicklistNodeAllowMerge(target, target->next, fill)) {
        _quicklistZiplistMerge(quicklist, target, target->next);
    }
}

// 将一个node进行拆分
// after = 1，切割出来[offset+1, END]并返回，源node保留[0, offset]
// after = 0，切割出来[0, offset]并返回，源node保留[offset+1, END]
REDIS_STATIC quicklistNode* _quicklistSplitNode(quicklistNode* node, int offset, int after) {
    size_t zl_sz = node->sz;

    quicklistNode* new_node = quicklistCreateNode();
    new_node->zl = zmalloc(zl_sz);

    memcpy(new_node->zl, node->zl, zl_sz);

    // 分割的两端的起始和结束位置（-1表示结尾）
    int orig_start = after ? offset + 1 : 0;    // 起始
    int orig_extent = after ? -1 : offset;      // 结束

    int new_start = after ? 0 : offset;         // 起始
    int new_extent = after ? offset + 1 : -1;   // 结束

    // 截取一段，将orig表示的那段删除，留下剩下的那段
    node->zl = ziplistDeleteRange(node->zl, orig_start, orig_extent);
    node->count = ziplistLen(node->zl);
    quicklistNodeUpdateSz(node);

    // 截取另一段，将new表示的那段删除，留下剩下的那段
    new_node->zl = ziplistDeleteRange(new_node->zl, new_start, new_extent);
    new_node->count = ziplistLen(new_node->zl);
    quicklistNodeUpdateSz(new_node);

    return new_node;
}

// 在给定的entry（相当于给定了某个元素）之后或之前插入value
REDIS_STATIC void _quicklistInsert(quicklist* quicklist, quicklistEntry* entry, void* value, const size_t sz, int after) {
    int full = 0, at_tail = 0, at_head = 0, full_next = 0, full_prev = 0;
    int fill = quicklist->fill;
    quicklistNode* node = entry->node;
    quicklistNode* new_node = NULL;

    if (!node) {    // 给定entry为空，即表示quicklist当前为空。此时插入一个元素，则该元素就是此quicklist中唯一的元素
        new_node = quicklistCreateNode();
        new_node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);
        _quicklistInsertNode(quicklist, NULL, new_node, after);
        new_node->count++;
        quicklist->count++;
        return;
    }

    // 下面是设置各种标志位，这些标志位共同决定这个元素应该插入到哪里
    if (!_quicklistNodeAllowInsert(node, fill, sz)) {
        full = 1;   // 给定的节点不允许再插入元素了，即表示当前节点已满
    }

    if (after && (entry->offset == node->count)) {
        at_tail = 1; // 在给定节点的ziplist结尾插入元素
        if (!_quicklistNodeAllowInsert(node->next, fill, sz)) {
            full_next = 1;  // 下一个节点满了
        }
    }

    if (!after && (entry->offset == 0)) {
        at_head = 1;    // 在当前节点的ziplist头部插入元素
        if (!_quicklistNodeAllowInsert(node->prev, fill, sz)) {
            full_prev = 1;  // 前一个节点满了
        }
    }

    if (!full && after) {  // 当前节点没有满，直接在当前节点指定的元素后面插入
        // 定位到给定的entry的后一个元素
        unsigned char* next = ziplistNext(node->zl, entry->zi);
        if (next == NULL)   // 给定entry的后一个元素是ziplist结尾
            node->zl = ziplistPush(node->zl, value, sz, ZIPLIST_TAIL);
        else    // 给定entry的后一个元素不是ziplist结尾
            // 在给定entry的后一个元素的前面插入元素
            node->zl = ziplistInsert(node->zl, next, value, sz);

        node->count++;
        quicklistNodeUpdateSz(node);
    } else if (!full && after)  {   // 当前节点没有满，直接在当前节点指定的元素前面插入
        node->zl = ziplistInsert(node->zl, entry->zi, value, sz);
        node->count++;
        quicklistNodeUpdateSz(node);
    } else if (full && at_tail && node->next && !full_next && after) {  // 要在后面插入，并且当前节点满了，并且当前节点的下一个节点没有满，就在当前节点下一个节点的头部插入
        new_node = node->next;
        new_node->zl = ziplistPush(new_node->zl, value, sz, ZIPLIST_HEAD);
        new_node->count++;
        quicklistNodeUpdateSz(new_node);
    } else if (full && at_head && node->prev && !full_prev && !after) { // 要在前面插入，并且当前节点满了，并且当前节点的前一个节点没有满，就在当前节点前一个节点的尾部插入
        new_node = node->prev;
        new_node->zl = ziplistPush(new_node->zl, value, sz, ZIPLIST_TAIL);
        new_node->count++;
        quicklistNodeUpdateSz(new_node);
    } else if (full && ((at_tail && node->next && full_next && after) || (at_head && node->prev && full_prev && !after))) { // 当前节点满了，在开头插入前面一个节点满了，或者在结尾插入后面一个节点满了，需要新建node插入了
        new_node = quicklistCreateNode();
        new_node->zl = ziplistPush(ziplistNew(), value, sz, ZIPLIST_HEAD);
        new_node->count++;
        quicklistNodeUpdateSz(new_node);
        _quicklistInsertNode(quicklist, node, new_node, after); // 在给定node前面或后面插入新的节点
    } else if (full) {  // 在当前节点中间插入，但是当前节点满了，就需要对当前节点进行拆分
        new_node = _quicklistSplitNode(node, entry->offset, after); // 以给定的元素为分界点进行拆分，返回的是拆分出来的node
        new_node->zl = ziplistPush(new_node->zl, value, sz, after ? ZIPLIST_HEAD : ZIPLIST_TAIL);
        new_node->count++;
        quicklistNodeUpdateSz(new_node);
        _quicklistInsertNode(quicklist, node, new_node, after); // 将拆分出来的node重新插入到quicklist中
        _quicklistMergeNodes(quicklist, node);  // 再将当前节点node与其前后各两个节点尝试进行合并（合并的目的是避免quicklist节点太多，造成quicklist链表太长了，造成查询遍历链表慢）
    }
    quicklist->count++;
}

void quicklistInsertBefore(quicklist* quicklist, quicklistEntry* entry, void* value, const size_t sz) {
    _quicklistInsert(quicklist, entry, value, sz, 0);
}

void quicklistInsertAfter(quicklist* quicklist, quicklistEntry* entry, void* value, const size_t sz) {
    _quicklistInsert(quicklist, entry, value, sz, 1);
}

// 从start索引开始，删除count个元素？
// start为正，表示从头到尾数start个位置开始往后删除count个元素
// start为负，表示从尾到头数start个位置开始往后删除count个元素
int quicklistDelRange(quicklist* quicklist, const long start, const long count) {
    if (count <= 0)
        return 0;

    unsigned long extent = count;

    if (start >= 0 && extent > (quicklist->count - start)) {    // start为正，要删除的元素总和 大于 从start(从头到尾)开始到结尾的元素的总和
        extent = quicklist->count - start;  // 将删除的元素数量限制到 从start开始到结尾的元素的总和
    } else if (start < 0 && extent > (unsigned  long)(-start)) {       // start为负，要删除的元素总和 大于 从start(从尾到头)开始到结尾的元素的总和
        extent = -start;    // 将删除的元素数量限制到 从start开始到结尾的元素的总和
    }

    quicklistEntry entry;
    // 找到索引为start的元素
    if (!quicklistIndex(quicklist, start, &entry))
        return 0;

    // 获取索引为start的元素
    quicklistNode* node = entry.node;

    // 一个node一个node的循环删除
    while (extent) {
        quicklistNode* next = node->next;

        unsigned long del;
        int delete_entire_node = 0;

        // 计算本node内需要删除的元素数量del
        if (entry.offset == 0 && extent >= node->count) {   // 开始位置在当前node开头，并且删除的元素要大于本node的元素数量，表示该node要整个被删掉
            delete_entire_node = 1;
            del = node->count;
        } else if (entry.offset > 0 && extent >= node->count) { // 删除位置在node中，并且删除的元素要大于本node的元素数量
            del = node->count - entry.offset;
        } else if (entry.offset < 0) { //start为负， 从尾到头 -offset的位置
            del = -entry.offset;    // entry.offset < 0 && extent >= node->count

            if (del > extent)   // entry.offset < 0 && extent < node->count  即需要删除的元素数量 小于从-offset（从尾到头）位置开始往后本node中剩余的元素数量
                del = extent;
        } else {    // entry.offset >= 0 && extent < node->count 即需要删除的元素数量 小于 本node中剩余的元素数量
            del = extent;
        }

        if (delete_entire_node) {
            _quicklistDelNode(quicklist, node);
        } else {
            node->zl = ziplistDeleteRange(node->zl, entry.offset, del);
            quicklistNodeUpdateSz(node);
            node->count -= del;
            quicklistDeleteIfEmpty(quicklist, node);
        }
        extent -= del;  // 更新还剩下需要删除的元素数量

        node = next;

        entry.offset = 0;
    }
    return 1;
}

// 比较给定的元素p2是否和p1项(ziplist项)中存储的元素相同
int quicklistCompare(unsigned char* p1, unsigned char* p2, int p2_len) {
    return ziplistCompare(p1, p2, p2_len);
}


quicklistIter* quicklistGetIterator(const quicklist* quicklist, int direction) {
    quicklistIter* iter;

    iter = zmalloc(sizeof(*iter));

    if (direction == AL_START_HEAD) {
        iter->current = quicklist->head;
        iter->offset = 0;   // 从头往尾遍历，offset为0，表示第一个元素
    } else if (direction == AL_START_TAIL) {
        iter->current = quicklist->tail;
        iter->offset = -1;  // 从尾往头遍历，offset为-1，表示最后一个元素
    }

    iter->direction = direction;
    iter->quicklist = quicklist;

    iter->zi = NULL;

    return iter;
}

quicklistIter* quicklistGetIteratorAtIdx(const quicklist* quicklist, const int direction, const long long idx) {
    quicklistEntry entry;

    if (quicklistIndex(quicklist, idx, &entry)) {   // 获取index位置元素所在的node以及在该node中偏移量的信息
        quicklistIter* base = quicklistGetIterator(quicklist, direction);
        base->zi = NULL;
        base->current = entry.node;
        base->offset = entry.offset;
        return base;
    } else {    // 没找该索引对应的元素
        return NULL;
    }
}

void quicklistReleaseIterator(quicklistIter* iter) {
    zfree(iter);
}


int quicklistNext(quicklistIter* iter, quicklistEntry* entry) {
    initEntry(entry);

    if (!iter) {
        return 0;
    }

    entry->quicklist = iter->quicklist;
    entry->node = iter->current;

    if (!iter->current)
        return 0;

    unsigned char* (*nextFn) (unsigned char*, unsigned char*) = NULL;
    int offset_update = 0;

    if (!iter->zi) {    // 如果当前迭代器的zi为空，表明迭代器当前的node还没开始倍遍历，就直接使用迭代器的offset去索引ziplist的项，作为当前迭代器的zi，即获取第一项作为当前迭代的项
        iter->zi = ziplistIndex(iter->current->zl, iter->offset);
    } else {    // 用ziplist的函数 获取下一项
        if (iter->direction == AL_START_HEAD) {
            nextFn = ziplistNext;
            offset_update = 1;
        } else if (iter->direction == AL_START_TAIL) {
            nextFn = ziplistPrev;
            offset_update = 1;
        }
        iter->zi = nextFn(iter->current->zl, iter->zi);
        iter->offset += offset_update;
    }

    entry->zi = iter->zi;
    entry->offset = iter->offset;

    if (iter->zi) { // 迭代器当前node中还有元素，next获取的不是NULL
        ziplistGet(entry->zi, &entry->value, &entry->sz, &entry->longval);  // 获取元素值
        return 1;
    } else {    // 迭代器当前node中没有元素了，next获取的是NULL，此时就需要去下一个node中获取下一个元素
        if (iter->direction == AL_START_HEAD) {
            iter->current = iter->current->next;
            iter->offset = 0;
        } else if (iter->direction == AL_START_TAIL) {
            iter->current = iter->current->prev;
            iter->offset = -1;
        }
        iter->zi = NULL;
        return quicklistNext(iter, entry);
    }
}