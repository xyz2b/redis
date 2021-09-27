//
// Created by xyzjiao on 9/22/21.
//

#include <sys/time.h>
#include "server.h"

// 全局共享变量
struct sharedObjectsStruct shared;
struct redisServer server;

dictType hashDictType = {
        dictSdsHash,
        NULL,
        NULL,
        dictSdsKeyCompare,
        dictSdsDestructor,
        dictSdsDestructor,
};


// 获取以微秒为单位的当前UNIX时间戳
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

// 获取以毫秒为单位的当前UNIX时间戳
mstime_t mstime(void) {
    return ustime() / 1000;
}


uint64_t dictSdsHash(const void* key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

int dictSdsKeyCompare(void* privdata, const void* key1, const void* key2) {
    int l1, l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcpy(key1, key2, l1) == 0;
}

void dictSdsDestructor(void* privdata, void* val) {
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

int htNeedResize(dict* dict) {
    long long size, used;

    size = dictSlots(dict);
    used = dictSize(dict);

    return (size > DICT_HT_INITIAL_SIZE && (used * 100 / size < HASHTABLE_MIN_FILL));
}