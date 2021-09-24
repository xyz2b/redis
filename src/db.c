//
// Created by xyzjiao on 9/22/21.
//

#include <stdlib.h>
#include "server.h"
#include "assert.h"

// 触发 key对应的value被修改 的信号
void signalModifiedKey(redisDb* db, robj* key) {
    // 通知所有watch该key的client
    touchWatchKey(db, key);
}

void updateLFU(robj* val) {

}

robj* lookupKeyWrite(redisDb* db, robj* key) {
    expireIfNeeded(db, key);
    return lookupKey(db, key, LOOKUP_NONE);
}

int expireIfNeeded(redisDb* db, robj* key) {

}

robj* lookupKey(redisDb* db, robj* key, int flags) {
    dictEntry* de = dictFind(db->dict, key->ptr);
    if (de) {
        robj* val = dictGetVal(de);

        // 没有在做rdb或aof，且不会触发写时复制，就更新该value对象的访问时间
        if (server.rdb_child_pid == -1 && server.aof_child_pid == -1 && !(flags & LOOKUP_NOTOUCH)) {
            if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
                updateLFU(val);
            } else {
                val->lru = LRU_CLOCK();
            }
        }
        return val;
    } else {
        return NULL;
    }
}

// 新增key-value
void dbAdd(redisDb* db, robj* key, robj* val) {
    sds copy = sdsdup(key->ptr);
    int retval = dictAdd(db->dict, copy, val);

    assert(retval == DICT_OK);
    if (val->type == OBJ_LIST || val->type == OBJ_ZSET)
        signalKeyAsReady(db, key);
    if (server.cluster_enabled) slotToKeyAdd(key);
}

// 已存在key覆盖之前的value，需要释放之前的value空间
void dbOverwrite(redisDb* db, robj* key, robj* val) {
    dictEntry* de = dictFind(db->dict, key->ptr);

    assert(de != NULL);

    // 为了释放value对象而使用
    dictEntry auxentry = *de;   // 这里复制一份的目的是 当在dict中设置新的value对象指针之后（覆盖了老的value对象的指针），老的value对象就没有指针指向它，访问不到，所以没办法释放

    // 获取老的value对象，为了惰性删除value对象而使用
    robj* old = dictGetVal(de);
    if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
        val->lru = old->lru;
    }
    // 设置新的value对象，覆盖老的对象指针
    dictSetVal(db->dict, de, val);

    // 惰性删除value对象
    if (server.lazyfree_lazy_server_del) {
        freeObjAsync(old);
        dictSetVal(db->dict, &auxentry, NULL);
    }

    // 不是惰性删除，直接释放对象（惰性删除会把value置为NULL，所以这里再次释放NULL，没有什么影响）
    dictFreeVal(db->dict, &auxentry);
}

void setKey(redisDb* db, robj* key, robj* val) {
    if (lookupKeyWrite(db, key) == NULL) {
        dbAdd(db, key, val);
    } else {
        dbOverwrite(db, key, val);
    }
}


void slotToKeyAdd(robj* key) {

}

void setExpire(client* c, redisDb* db, robj* key, long long when) {
    dictEntry* kde, *de;

    kde = dictFind(db->dict, key->ptr);
    assert(kde != NULL);
    // key存在就返回该key，key不存在就新增（db->expires dict）
    de = dictAddOrFind(db->expires, dictGetKey(kde));
    // 超时时间作为key的value（db->expires）
    dictSetSignedIntegerVal(de, when);



}

robj* lookupKeyReadWithFlags(redisDb* db, robj* key, int flags) {
    robj* val;

    //
    if (expireIfNeeded(db, key) == 1) {
        if (server.masterhost == NULL) {
            server.stat_keyspace_misses++;
            return NULL;
        }

        if (server.curren_client && server.curren_client != server.master && server.curren_client->cmd && server.curren_client->cmd->flags & CMD_READONLY) {
            server.stat_keyspace_misses++;
            return NULL;
        }
    }

    val = lookupKey(db, key, flags);
    if (val == NULL)
        server.stat_keyspace_misses++;
    else
        server.stat_keyspace_hists++;

    return val;
}

robj* lookupKeyRead(redisDb* db, robj* key) {
    return lookupKeyReadWithFlags(db, key, LOOKUP_NONE);
}

robj* lookupKeyReadOrReply(client *c, robj* key, robj* reply) {
    robj* o = lookupKeyRead(c->db, key);
    if (!o) addReply(c, reply);
    return o;
}

robj* lookupKeyWriteOrReply(client* c, robj* key, robj* reply) {
    robj* o = lookupKeyWrite(c->db, key);
    if (!o) addReply(c, reply);
    return o;
}

robj* lookupKeyRead(redisDb* db, robj* key) {
    return lookupKeyReadWithFlags(db, key, LOOKUP_NONE);
}

int dbDelete(redisDb* db, robj* key) {

}