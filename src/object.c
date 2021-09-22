//
// Created by xyzjiao on 9/22/21.
//
#include "server.h"
#include "redisassert.h"
#include "util.h"

// 尝试对对象进行编码
robj* tryObjectEncoding(robj* o) {
    long value;
    sds s = o->ptr;
    size_t len;

    assert(o->type == OBJ_STRING);


    // 是否是sds的编码类型
    if (!sdsEncodingObject(o)) return o;

    // 共享的对象，对其进行编码不安全
    if (o->refcout > 1) return o;

    len = sdslen(s);
    // 如果给的value字符串可以转换成long类型的integer，同时value字符串的长度小于等于20
    //
    if (len <= 20 && string2l(s, len, &value)) {
        // 共享已有的integer对象
        if ((server.maxmemory == 0 || !(server.maxmemory_policy & MAXMEMORY_FLAG_NO_SHARED_INTEGERS)) && value >= 0 && value < OBJ_SHARED_INTEGERS) {

        }
    }

}