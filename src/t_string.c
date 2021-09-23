//
// Created by xyzjiao on 9/22/21.
//

#include <stddef.h>
#include "server.h"

#define OBJ_SET_NO_FLAGS 0
#define OBJ_SET_NX (1<<0)
#define OBJ_SET_XX (1<<1)
#define OBJ_SET_EX (1<<2)
#define OBJ_SET_PX (1<<3)

// flags: xx/nx/ex/px标志位
// expire: 过期时间
// unit: 时间单位，秒或毫秒
void setGenericCommand(client* c, int flags, robj* key, robj* val, robj* expire, int unit, robj* ok_reply, robj* abort_reply) {
    long long milliseconds = 0;

    // 如果有设置过期时间
    if (expire) {
        // 从字符串对象中获取过期时间存储在milliseconds中
        if (getLongLongFromObjectOrReply(c, expire, &milliseconds, NULL) != C_OK) {
            return;
        }
        if (milliseconds <= 0) {
            addReplyErrorFormat(c, "invalid expire time in %s", c->cmd->name);
            return;
        }
        if (unit == UNIT_SECONDS) milliseconds *= 1000;
    }

    // nx 且 键已存在  或  xx 且 键不存在
    if ((flags && OBJ_SET_NX && lookupKeyWrite(c->db, key) != NULL) || (flags & OBJ_SET_XX && lookupKeyWrite(c->db, key) == NULL)) {
        addReply(c, abort_reply ? abort_reply : shared.nullbulk);
        return;
    }
    setKey(c->db, key, val);
    server.dirty++;
    if (expire) setExpire(c, c->db, key, mstime() + milliseconds);
    notifyKeyspaceEvent(NOTIFY_STRING, "set", key, c->db->id);
    if (expire) notifyKeyspaceEvent(NOTIFY_GENERIC, "expire", key, c->db->id);
    addReply(c, ok_reply ? ok_reply : shared.ok);
}

/**
 * set key value [NX] [XX] [EX <seconds>] [PX <milliseconds>]
 * EX second ：设置键的过期时间为 second 秒。 SET key value EX second 效果等同于 SETEX key second value 。
 * PX millisecond ：设置键的过期时间为 millisecond 毫秒。 SET key value PX millisecond 效果等同于 PSETEX key millisecond value 。
 * NX ：只在键不存在时，才对键进行设置操作。 SET key value NX 效果等同于 SETNX key value 。
 * XX ：只在键已经存在时，才对键进行设置操作。
 * */
void setCommand(client *c) {
    int j;
    robj* expire = NULL;
    int unit = UNIT_SECONDS;
    int flags = OBJ_SET_NO_FLAGS;

    for (j = 3; j < c->argc; j++) {
        char* a = c->argv[j]->ptr;
        robj* next = (j == c->argc - 1) ? NULL : c->argv[j+1];

        // nx，与xx冲突
        if ((a[0] == 'n' || a[0] == 'N') && (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && !(flags & OBJ_SET_XX)) {
            flags |= OBJ_SET_NX;
        } else if ((a[0] == 'x' || a[0] == 'X') && (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && !(flags & OBJ_SET_NX)) { // xx，与nx冲突
            flags |= OBJ_SET_XX;
        } else if ((a[0] == 'e' || a[0] == 'E') && (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && !(flags & OBJ_SET_PX) && next) { // ex，与px冲突
            flags |= OBJ_SET_EX;
            unit = UNIT_SECONDS;
            expire = next;
            j++;
        } else if ((a[0] == 'p' || a[0] == 'P') && (a[1] == 'x' || a[1] == 'X') && a[2] == '\0' && !(flags & OBJ_SET_EX) && next) {
            flags |= OBJ_SET_PX;
            unit = UNIT_MILLISECONDS;
            expire = next;
            j++;
        } else {
            addReply(c, shared.syntaxerr);
            return;
        }
    }

    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, flags, c->argv[1], c->argv[2], expire, unit, NULL, NULL);
}

void setnxCommand(client* c) {
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setGenericCommand(c, OBJ_SET_NX, c->argv[1], c->argv[2], NULL, 0, shared.cone, shared.czero);
}

void setexCommand(client* c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, OBJ_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_SECONDS, NULL, NULL);
}

void psetexCommand(client* c) {
    c->argv[3] = tryObjectEncoding(c->argv[3]);
    setGenericCommand(c, OBJ_SET_NO_FLAGS, c->argv[1], c->argv[3], c->argv[2], UNIT_MILLISECONDS, NULL, NULL);
}

int getGenericCommand(client* c) {
    robj* o;

    // 没找到key
    if((o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk)) == NULL)
        return C_OK;

    // 找到key
    if (o->type != OBJ_STRING) {
        addReply(c, shared.wrongtypeerr);
        return C_ERR;
    } else {
        addReply(c, o);
        return C_OK;
    }
}

void getCommand(client* c) {
    getGenericCommand(c);
}

void getsetCommand(client* c) {
    if (getGenericCommand(c) == C_ERR) return;
    c->argv[2] = tryObjectEncoding(c->argv[2]);
    setKey(c->db, c->argv[1], c->argv[2]);
    notifyKeyspaceEvent(NOTIFY_STRING, "set", c->argv[1], c->db->id);
    server.dirty++;
}