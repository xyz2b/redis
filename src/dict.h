//
// Created by xyzjiao on 9/2/21.
//

#ifndef REDIS_DICT_H
#define REDIS_DICT_H
#include <stdint.h>

#define DICT_OK 0
#define DICT_ERR 1

typedef struct dictType {
    uint64_t (*hashFunction) (const void* key);
    void* (*keyDup) (void* privdata, const void* key);
    void* (*valDup) (void* privdata, const void* obj);
    int (*keyCompare) (void* privdata, const void* key1, const void* key2);
    void (*keyDestructor) (void*privdata, const void* key);
    void (*valDestructor) (void* privdata, const void* obj);
} dictType;

typedef struct dictEntry {
    void* key;
    union {
        void* val;
        uint64_t u64;
        int64_t s64;
        double d;
    } v;
    struct dictEntry* next;
} dictEntry;

typedef struct dictht {
    dictEntry** table;      // 存储指向dictEntry指针的数组
    unsigned long size;
    unsigned long sizemask;
    unsigned long used;
} dictht;

typedef struct dict {
    dictType* type;
    void* privdata;
    dictht ht[2];
    long rehashidx;
    unsigned long iterators;
} dict;

typedef struct dictIterator {
    dict* d;
    long index;
    int table, safe;
    dictEntry* entry, *nextEntry;
    long long fingerprint;
} dictIterator;

// scan dict dictEntry
typedef void (dictScanFunction) (void* privdata, const dictEntry* de);
// scan dict table
typedef void (dictScanBucketFunction) (void* privdata, dictEntry** bucketref);

#define DICT_HT_INITIAL_SIZE 4

#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ?         \
    (d)->type->keyCompare((d)->privdata, key1, key2) : \
    (key1) == (key2))
#define dictSetKey(d, entry, _key_) do { \
    if((d)->type->keyDup)                \
        (entry)->key = (d)->type->keyDup((d)->privdata, _key_); \
    else                                         \
        (entry)->key = (_key_);                                         \
} while(0)
#define dictSetVal(d, entry, _val_) do { \
    if((d)->type->valDup)                \
        (entry)->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else                                         \
        (entry)->v.val = (_val_);                                          \
} while(0)

#define dictHashKey(d, key) (d)->type->hashFunction(key)
#define dictIsRehashing(d) ((d)->rehashidx != -1)

// 创建
dict* dictCreate(dictType* type, void* privDataPtr);
// 增
int dictAdd(dict* d, void* key, void* val);
dictEntry* dictAddOrFind(dict* d, void* key);
dictEntry* dictAddRaw(dict* d, void* key, dictEntry** existing);
// 增/改
int dictReplace(dict* d, void* key, void* val);
// 删
int dictDelete(dict* d, const void* key);
dictEntry* dictUnlink(dict* d, const void* key);
// 查
dictEntry* dictFind(dict* d, const void* key);
// 釋放dict
int dictRelease(dict* d);

void dictFreeUnlinkedEntry(dict* d, dictEntry* he);

// 擴容
int dictExpand(dict* d, unsigned long size);
// rehash
int dictRehash(dict* d, int n);

void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
uint64_t dictGetHash(dict *d, const void *key);

// 迭代器
dictIterator* dictGetIterator(dict* d);
dictIterator* dictGetSafeIterator(dict* d);
dictEntry* dictNext(dictIterator* iter);
void dictReleaseIterator(dictIterator* iter);
#endif //REDIS_DICT_H
