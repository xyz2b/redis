//
// Created by xyzjiao on 9/22/21.
//

#include <stddef.h>
#include "server.h"
#include "networking.h"

#define OBJ_SET_NO_FLAGS 0
#define OBJ_SET_NX (1<<0)
#define OBJ_SET_XX (1<<1)
#define OBJ_SET_EX (1<<2)
#define OBJ_SET_PX (1<<3)

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

}