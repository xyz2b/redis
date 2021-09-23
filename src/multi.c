//
// Created by xyzjiao on 9/23/21.
//

#include "server.h"
#include "dict.h"

// 通知watch该key的客户端
void touchWatchKey(redisDb* db, robj* key) {
    list* clients;
    listIter li;
    listNode* ln;

    if (dictSize(db->watch_keys) == 0) return;
    // 从字典中获取key对应的value值，如果不存在key，返回NULL
    clients = dictFetchValue(db->watch_keys, key);
    if (!clients) return;


    // 获取链表迭代器
    listRewind(clients, &li);

    // 将所有watch该key的client的标志位 置为CLIENT_DIRTY_CAS
    while (ln = listNext(&li)) {
        client* c = listNodeValue(ln);
        c->flags |= CLIENT_DIRTY_CAS;
    }
}