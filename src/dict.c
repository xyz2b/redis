//
// Created by xyzjiao on 9/2/21.
//

#include <limits.h>
#include <string.h>
#include "redisassert.h"
#include "dict.h"
#include "zmalloc.h"

// dict_can_resize可以控制是hashtable的冲突因子是1还是5，如果dict_can_resize为1，则冲突因子为1，即元素个数大于table数组的大小就扩容；如果dict_can_resize为0，则冲突因子为5，即元素个数大于table数组的大小的5倍才扩容
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/*------------------------ private prototypes ----------------------------*/

static int _dictInit(dict* d, dictType* type, void* privDataPtr);


static void _dictRehashStep(dict *pDict);

static long _dictKeyIndex(dict *d, void *key, uint64_t hash, dictEntry **existing);

static int _dictExpandIfNeeded(dict *d);

int _dictClear(dict *d, dictht *ht, void (callback)(void *));

/*--------------- hash function ---------------------------*/
// hash种子
static uint8_t dict_hash_function_seed[16];

void dictSetHashFunctionSeed(uint8_t* seed) {
    memcpy(dict_hash_function_seed, seed, sizeof(dict_hash_function_seed));
}

uint8_t* dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

// hash函数，计算key的哈希值
uint64_t siphash(const uint8_t* in, const size_t inlen, const uint8_t* k);
uint64_t siphash_nocase(const uint8_t* in, const size_t inlen, const uint8_t* k);

uint64_t dictGenHashFunction(const void* key, int len) {
    return siphash(key, len, dict_hash_function_seed);
}

uint64_t dictGenCaseHashFunction(const void* key, int len) {
    return siphash_nocase(key, len, dict_hash_function_seed);
}

/*------------------- api implementation ----------------------------*/

// 重置dictht
static void _dictReset(dictht *ht) {
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

// 创建dict
dict* dictCreate(dictType* type, void* privDataPtr) {
    dict* d = zmalloc(sizeof(*d));
    _dictInit(d, type,  privDataPtr);
    return d;
}

// 初始化dict
int _dictInit(dict* d, dictType* type, void* privDataPtr) {
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;
    return DICT_OK;
}

// dict新增key-val键值对，如果key已經存在，返回錯誤，即不覆蓋已存在的key
int dictAdd(dict* d, void* key, void* val) {
    dictEntry* entry = dictAddRaw(d, key, NULL);

    if (!entry) return DICT_ERR;
    // 设置dictEntry的value
    dictSetVal(d, entry, val);
    return DICT_OK;
}

// 如果添加的key已经存在了，则返回NULL，existing中返回已经存在key的dictEntry；否则在hash table中新增一个节点，病返回新增的这个节点entry
dictEntry* dictAddRaw(dict* d, void* key, dictEntry** existing) {
    long index;
    dictEntry* entry;
    dictht* ht;

    // 判断是否在rehash，如果在rehash，就需要在每次执行新增操作前执行rehash的步骤（渐进式rehash）
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 获取key在dict中table的索引，如果返回为-1标识该key已经存在于hash table中
    if ((index = _dictKeyIndex(d, key, dictHashKey(d, key), existing)) == -1)
        return NULL;

    // 如果是dict在rehash，新添加的节点都加在ht[1]中，ht[0]中不在新增key；如果不在rehash，则key都存在ht[0]中
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];

    entry = zmalloc(sizeof(*entry));
    // 同一个hash table槽位存多个key，以链表形式存储
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    // 设置dictEntry的key
    dictSetKey(d, entry, key);
    return entry;
}

// 根据所给的key以及其hash值，计算出其在hash table中的索引，如果该key已经存在于hash table中，则用existing指针返回已经在的node节点
static long _dictKeyIndex(dict *d, void *key, uint64_t hash, dictEntry **existing) {
    unsigned long idx, table;
    dictEntry* he;
    if (existing) *existing = NULL;

    // dict table是否需要扩展
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    for (table = 0; table <= 1; table++) {
        idx = hash & d->ht[table].sizemask;
        // 在hash table中查找是否已经存在已经给的key
        he = d->ht[table].table[idx];
        // table table对应的槽位是链表的情况，需要循环遍历链表查找key
        while (he) {
            if (key == he->key || dictCompareKeys(d, key, he->key)) {
                if (existing) *existing = he;
                return -1;
            }
            he = he->next;
        }
        // 如果dict没有在rehash，就不需要再在ht[1]中查找了，没有rehash时，另一个ht也是空的
        if (!dictIsRehashing(d)) break;
    }

    return idx;
}

static int _dictExpandIfNeeded(dict *d) {
    // rehash進行中，直接return
    if(dictIsRehashing(d)) return DICT_OK;

    // 如果hashtable是空，就擴展到初始大小
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    // 如果dict存储的元素大于dict table数组的大小的5倍就需要进行扩容，(5是hashtable的冲突因子)
    // dict_can_resize可以控制是hashtable的冲突因子是1还是5，如果dict_can_resize为1，则冲突因子为1，即元素个数大于table数组的大小就扩容；如果dict_can_resize为0，则冲突因子为5，即元素个数大于table数组的大小的5倍才扩容
    if (d->ht[0].used >= d->ht[0].size && (dict_can_resize || d->ht[0].used / d->ht[0].size > dict_force_resize_ratio))
        return dictExpand(d, d->ht[0].used * 2);    // 扩容为已有元素的2倍

    return DICT_OK;
}

static void _dictRehashStep(dict *d) {
    if (d->iterators == 0) dictRehash(d, 1);
}

// 渐进式rehash，参数n表示一次rehash多少个hash table槽位，如果为1，即表示一次只rehash一个槽位，即hash table数组一个索引位置
// 返回值为0，表示rehash完整个table，返回值为1，表示没有rehash完整个table
int dictRehash(dict* d, int n) {
    int empty_visits = n * 10;  // 在一次rehash中最大遍历的空槽位的数目，如果遍历的空槽位数达到该值，则停止本次rehash

    if(!dictIsRehashing(d)) return 0;

    // d->ht[0].used 如果为0，说明ht[0]中的元素都挪到ht[1]了，rehash结束
    while (n-- && d->ht[0].used != 0) {
        dictEntry* de, *nextde;

        // 防止rehashidx索引越界，rehashidx最大应该就是ht[0]的大小-1，即指向hashtable最后一个槽位
        assert(d->ht[0].size > (unsigned long)d->rehashidx);

        while (d->ht[0].table[d->rehashidx] == NULL) {
            d->rehashidx++;
            if (--empty_visits == 0) return 1;
        }

        // 取出需要rehash的槽位的元素链表
        de = d->ht[0].table[d->rehashidx];
        while (de) {
            uint64_t h;

            nextde = de->next;

            // 计算需要rehash的元素在ht[1]中的索引
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            // 将rehash的元素添加到ht[1]对应槽位的链表头
            de->next = d->ht[1].table[h];
            d->ht[1].table[n] = de;
            d->ht[0].used--;
            d->ht[1].used++;
            de = nextde;
        }
        // 从ht[0]中删除rehash元素的指针
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }

    // 检查是否rehash完整个table
    if (d->ht[0].used == 0) {
        // 释放ht[0]的槽位
        zfree(d->ht[0].table);
        // 将ht[0]指向ht[1]的table
        d->ht[0] = d->ht[1];
        // 重置ht[1]，断开ht[1] table的指针
        _dictReset(&d->ht[1]);
        d->rehashidx = -1;
        return 0;   // rehash完整个table
    }

    // 没有rehash完整个table
    return 1;
}

// 最终hashtable的大小为 大于等于size且最接近size的2的N次方的值
static unsigned long _dictNextPower(unsigned long size) {
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX + 1LU;
    while (1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

int dictExpand(dict* d, unsigned long size) {
    // 如果正在rehash，或者扩容的size小于当前元素的数量，直接返回错误
    if (dictIsRehashing(d) || d->ht[0].used > size) return DICT_ERR;

    dictht n;   // new hash table
    unsigned long realsize = _dictNextPower(size);

    // 如果扩容后的大小和扩容前相同，返回错误
    if (realsize == d->ht[0].size) return DICT_ERR;

    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(1, realsize * sizeof(dictEntry*));
    n.used = 0;

    // 如果ht[0]为NULL，这里并不是表示rehash，而是hashtable第一次初始化
    if (d->ht[0].table == NULL) {
        d->ht[0] = n;
        return DICT_OK;
    }

    // 开始rehash
    d->ht[1] = n;
    d->rehashidx = 0;
    return DICT_OK;
}

// 删除指定key的节点，并返回该节点
static dictEntry* dictGenericDelete(dict* d, const void* key, int nofree) {
    uint64_t h, idx;
    dictEntry* he, *prevHe;
    int table;

    if (d->ht[0].used == 0 && d->ht[1].used == 0) return NULL;

    // 判断是否在rehash，如果在rehash，就需要在每次执行删除操作前执行rehash的步骤（渐进式rehash）
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算key的hash值
    h = dictHashKey(d, key);

    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        prevHe = NULL;
        // table table对应的槽位是链表的情况，需要循环遍历链表查找key
        while (he) {
            // 找到指定要删除的key
            if (key == he->key || dictCompareKeys(d, key, he->key)) {
                // 将当前遍历到的节点从列表中移除
                if (prevHe) // 链表中间的节点是我们要找的节点（使用prevHe保存当前遍历节点的前一个节点，方便将当前节点从链表中移除）
                    prevHe->next = he->next;
                else    // 链表头节点就是我们要找的节点
                    d->ht[table].table[idx] = he->next;
                // nofree为1，表示不释放删除的dictEntry
                if(!nofree) {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                    zfree(he);
                }
                d->ht[table].used--;
                // 指定nofree为0，就释放被删除的dictEntry，但是he没指定为NULL，所以这里返回的不为NULL，如果你想使用返回的被删除的dictEntry，nofree要为1
                return he;
            }
            // 保存当前遍历节点的前一个节点
            prevHe = he;
            he = he->next;
        }
        // 如果dict没有在rehash，就不需要再在ht[1]中查找了，没有rehash时，另一个ht也是空的
        if (!dictIsRehashing(d)) break;
    }

    return NULL;    // 没有找到指定要删除的key
}

// 删除key，並釋放key對應的dictEntry
int dictDelete(dict* d, const void* key) {
    return dictGenericDelete(d, key, 0) ? DICT_OK : DICT_ERR;
}

// 刪除key，但不釋放key對應的dictEntry
dictEntry* dictUnlink(dict* d, const void* key) {
    return dictGenericDelete(d, key, 1);
}

// 釋放dictEntry
void dictFreeUnlinkedEntry(dict* d, dictEntry* he) {
    if (he == NULL) return;
    dictFreeKey(d, he);
    dictFreeVal(d, he);
    zfree(he);
}

// 增加key，並返回新增key的dictEntry，如果key已存在，則返回其dictEntry。這裏僅僅是新增key，而不設置value
dictEntry* dictAddOrFind(dict* d, void* key) {
    dictEntry* entry, *existing;
    entry = dictAddRaw(d, key, &existing);
    return entry ? entry : existing;
}

// 添加key-value，相同key會覆蓋
int dictReplace(dict* d, void* key, void* val) {
    dictEntry* entry, *existing, auxentry;

    // 嘗試新增key，如果新增成功，說明key不存在，如果新增失敗說明key存在
    entry = dictAddRaw(d, key, &existing);
    if (entry) {
        dictSetVal(d, entry, val);
        return 1;
    }

    // 如果key已存在，直接修改key對應entry的val值即可
    auxentry = *existing;   // 直接復制一份新的dictEntry，爲了保存其中老的val
    dictSetVal(d, existing, val);   // 修改dict中key對應的dictEntry的val值
    // 釋放老的val
    dictFreeVal(d, &auxentry);
    return 0;
}

dictEntry* dictFind(dict* d, const void* key) {
    dictEntry* he;
    uint64_t h, idx, table;

    if (d->ht[0].used + d->ht[1].used == 0) return NULL;    // 空的dict

    // 判断是否在rehash，如果在rehash，就需要在每次执行查詢操作前执行rehash的步骤（渐进式rehash）
    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 计算key的hash值
    h = dictHashKey(d, key);

    // 查找key
    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        // table table对应的槽位是链表的情况，需要循环遍历链表查找key
        while (he) {
            // 找到指定的key
            if (key == he->key || dictCompareKeys(d, key, he->key)) {
                return he;
            }
            he = he->next;
        }
        // 如果dict没有在rehash，就不需要再在ht[1]中查找了，没有rehash时，另一个ht也是空的
        if (!dictIsRehashing(d)) break;
    }
    return NULL;
}

// 釋放dict
int dictRelease(dict* d) {
    _dictClear(d, &d->ht[0], NULL);
    _dictClear(d, &d->ht[1], NULL);
    zfree(d);
}

// 清空dict
int _dictClear(dict *d, dictht *ht, void (callback)(void *)) {
    unsigned long i;

    // 遍歷hash table
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        dictEntry* he, *nexthe;
        // 65535即0xFFFF，即i爲0，且callback不爲空時候，執行callback，即只在開始時執行一次
        if (callback && (i & 65535) == 0) callback(d->privdata);

        if ((he = ht->table[i]) == NULL) continue;
        // 遍歷hash table的一個槽位的鏈表，並釋放該鏈表上的所有dictEntry元素
        while (he) {
            nexthe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;
            he = nexthe;
        }
    }
    // 釋放hash table數組結構
    zfree(ht->table);
    _dictReset(ht);
    return DICT_OK;
}

void dictEmpty(dict *d, void(callback)(void*)) {
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}

void dictEnableResize(void) {
    dict_can_resize = 1;
}

void dictDisableResize(void) {
    dict_can_resize = 0;
}

uint64_t dictGetHash(dict *d, const void *key) {
    return dictHashKey(d, key);
}


// 计算dict指纹
// dict指纹是在计算指纹的时间点，根据dict目前的状态所得出的一个64bit的数值，表示了dict目前的状态（table指针、size、used）
// 对dict的修改会导致指纹的变化
long long dictFingerprint(dict* d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

// 获取dict迭代器
dictIterator* dictGetIterator(dict* d) {
    dictIterator* iter = zmalloc(sizeof(*iter));

    iter->d = d;
    iter->table = 0;        // hash table的索引，是ht[0]，还是ht[1]
    iter->index = -1;       // 表示迭代器还未开始迭代
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

// 获取安全的迭代器
dictIterator* dictGetSafeIterator(dict* d) {
    dictIterator* i = dictGetIterator(d);

    i->safe = 1;
    return i;
}

// 获取迭代器中下一个元素
dictEntry* dictNext(dictIterator* iter) {
    while (1) {
        // 遍历到hash table某个槽位对应的链表的尾部，就要从下一个槽位开始接着遍历（iter初始化时也走该逻辑，因为初始化时iter->entry也为NULL）
        if (iter->entry = NULL) {
            dictht *ht = &iter->d->ht[iter->table];
            // 还未开始迭代，处于初始化状态
            if (iter->index == -1 && iter->table == 0) {
                // TODO: safe？何为安全，dict的iter数量不为1时，不能修改？
                if (iter->safe)
                    iter->d->iterators++;
                else
                    // 计算dict指纹作为iter的指纹
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            iter->index++;
            // 迭代器的索引大于等于hash table的大小，即超出了table数组的索引（迭代到table数组的末尾）
            if (iter->index >= (long) ht->size) {
                // 如果是需要rehash，并且迭代器的迭代的是ht[0]，就需要迭代ht[1]，因为rehash时有部分元素是存在ht[1]中
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {    // 如果没有rehash或者迭代的是ht[1]，结合上层条件，即相当于迭代到了hash table末尾（不管是没有rehash的ht[0]的末尾，还是rehash的ht[1]的末尾都是hash table的末尾），直接跳出循环即可
                    break;
                }
            }
            // 设置迭代器的hashEntry元素为下一个槽位的链表开头的dictEntry元素
            iter->entry = ht->table[iter->index];
        } else {    // 没有遍历到当前槽位的链表尾部时
            // 获下一个hashEntry元素
            iter->entry = iter->nextEntry;
        }

        if (iter->entry) {
            // 保存迭代器当前迭代的元素的下一个元素
            iter->nextEntry = iter->entry->next;
            // 并返回当前元素
            return iter->entry;
        }
    }
    // 遍历到hash table结尾了，返回NULL
    return NULL;
}

// 释放迭代器
void dictReleaseIterator(dictIterator* iter) {
    // 如果迭代器不是初始状态（已经开始迭代）
    if (!(iter->index == -1 && iter->table == 0)) {
        // 如果是安全的迭代器
        if (iter->safe)
            iter->d->iterators--;
        else    // 非安全的迭代器，计算初始化iter时的dict指纹和当前dict的指纹是否不一样。即迭代器使用期间，dict是否发生了改变
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);
}


void* dictFetchValue(dict* d, const void* key) {
    dictEntry* he;

    he = dictFind(d, key);
    return he ? dictGetVal(he) : NULL;
}