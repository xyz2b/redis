//
// Created by xyzjiao on 9/22/21.
//

#include "server.h"

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
    robj *o = lookupKeyWrite(c->db, key);
    if (!o) addReply(c, reply);
    return o;
}

int dbDelete(redisDb* db, robj* key) {

}

void scanCallback(void* privdata, const dictEntry* de) {

}

// HSCAN key cursor [MATCH pattern] [COUNT count]   遍历hash
// SCAN cursor [MATCH pattern] [COUNT count]    遍历db的key
// TODO: 没有看太懂
void scanGenericCommand(client* c, robj* o, unsigned long cursor) {
    int i, j;
    list* keys = listCreate();
    listNode* node, *nextnode;
    long count = 10;
    sds pat = NULL;
    int patlen = 0, use_pattern = 0;
    dict* ht;

    // null: 遍历db的所有key
    // OBJ_SET、OBJ_HASH、OBJ_ZSET
    assert(o == NULL || o->type == OBJ_SET || o->type == OBJ_HASH || o->type == OBJ_ZSET);

    i = (o == NULL) ? 2 : 3;    // 根据命令的不同，选择跳过的参数个数，直接索引到cursor后面的参数 match、count

    // step 1 解析参数
    while (i < c->argc) {
        j = c->argc - i; // 除去cursor之前的参数，还剩下的参数个数。有match参数，那至少要占两个位置(match字符串和match的参数值)；有count参数，也至少要占两个位置(count字符串和count的参数值)
        if (!strcasecmp(c->argv[i]->ptr, "count") && j >= 2) {// j >= 2: count 会占两个参数位置，match也一样
            if (getLongFromObjectOrReply(c, c->argv[i+1], &count, NULL) != C_OK) {  // 获取count参数的值
                goto cleanup;
            }

            if (count < 1) {
                addReply(c, shared.syntaxerr);
                goto cleanup;
            }
        } else if (!strcasecmp(c->argv[i]->ptr, "match") && j >= 2) {
            pat = c->argv[i+1]->ptr;
            patlen = sdslen(pat);

            use_pattern = !(pat[0] == '*' && patlen == 1);

            i += 2;
        } else {
            addReply(c, shared.syntaxerr);
            goto cleanup;
        }
    }

    // step 2 遍历，获取结果集
    ht = NULL;
    if (o == NULL) { // o == NULL 遍历整个db的keyspace
        ht = c->db->dict;
    } else if (o->type == OBJ_HASH && o->encoding == OBJ_ENCODING_HT) { // 遍历一个hash中所有key/value
        ht = o->ptr;
        count *= 2; // key/value
    } else if (o->type == OBJ_SET && o->encoding == OBJ_ENCODING_HT) {  // 遍历一个set中所有元素
        ht = o->ptr;
    } else if (o->type == OBJ_ZSET && o->encoding == OBJ_ENCODING_SKIPLIST) { // 遍历一个zset中所有元素
        zset* zs = o->ptr;
        ht = zs->dict;
        count *= 2;  // key/value
    }

    if (ht) {
        void* privdata[2];
        // 有什么用？限制最大遍历次数？
        long maxiterations = count * 10;

        privdata[0] = keys; // 存储key的list
        privdata[1] = o;    // 存储对象

        do {
            cursor = dictScan(ht, cursor, scanCallback, NULL, privdata);
        } while (cursor && maxiterations-- && listLength(keys) < (unsigned long)count);
    } else if (o->type == OBJ_SET) {
        int pos = 0;
        int64_t ll;

        while(intsetGet(o->ptr, pos++, &ll)) listAddNodeTail(keys, createStringObjectFromLongLong(ll));
        cursor = 0;
    } else if (o->type == OBJ_HASH || o->type == OBJ_ZSET) {
        unsigned char* p = ziplistIndex(o->ptr, 0);
        unsigned char* vstr;
        unsigned int vlen;
        long long vll;

        while (p) {
            ziplistGet(p, &vstr, &vlen, &vll);
            listAddNodeTail(keys, (vstr != NULL) ? createStringObject((char*)vstr, vlen) : createStringObjectFromLongLong(vll));
            p = ziplistNext(o->ptr, p);
        }
        cursor = 0;
    } else {
        panic("Not handled encoding in SCAN");
    }

    // step 3 过滤元素
    // 从上面遍历得到的结果集中，过滤掉不匹配的结果，即从结果集中删除不匹配的结果
    node = listFirst(keys);
    while (node) {
        robj* kobj = listNodeValue(node);
        nextnode = listNextNode(node);
        int filter = 0;

        // filter element if it does not match the pattern
        if (!filter && use_pattern) {
            if (sdsEncodingObject(kobj)) {
                if (!stringmatchlen(pat, patlen, kobj->ptr, sdslen(kobj->ptr), 0))
                    filter = 1;
            } else {
                char buf[LONG_STR_SIZE];
                int len;

                assert(kobj->encoding == OBJ_ENCODING_INT);
                len = ll2string(buf, sizeof(buf), (long)kobj->ptr);
                if (!stringmatchlen(pat, patlen, buf, len, 0)) filter = 1;
            }
        }

        if (!filter && o == NULL && expireIfNeeded(c->db, kobj)) filter = 1;

        if (filter) {
            decrRefCount(kobj);
            listDelNode(keys, node);
        }

        if (o && (o->type == OBJ_ZSET || o->type == OBJ_HASH)) {
            node = nextnode;
            nextnode = listNextNode(node);

            if (filter) {
                kobj = listNodeValue(node);
                decrRefCount(kobj);
                listDelNode(keys, node);
            }
        }
        node = nextnode;
    }

    // step 4 返回结果给客户端
    // 返回过滤完的结果集给客户端以及获取剩余结果的cursor
    addReplyMultiBulkLen(c, 2);
    addReplyBulkLongLong(c, cursor);

    addReplyMultiBulkLen(c, listLength(keys));
    while ((node == listFirst(keys)) != NULL) {
        robj* kobj = listNodeValue(node);
        addReplyBulk(c, kobj);
        decrRefCount(kobj);
        listDelNode(keys, node);
    }


cleanup:
    listSetFreeMethod(keys, decrRefCountVoid);
    listRelease(keys);
}
