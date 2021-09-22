//
// Created by xyzjiao on 9/22/21.
//

#include <string.h>
#include "server.h"
#include "redisassert.h"
#include "util.h"
#include "quicklist.h"
#include "dict.h"
#include "zmalloc.h"

#define OBJ_ENCODING_EMBSTR_SIZE_LIMIT 44

robj* createEmbeddedStringObject(const char* ptr, size_t len) {
    robj* o = zmalloc(sizeof(robj) + sizeof(struct sdshdr8) + len + 1);
    struct sdshdr8* sh = (void*) (o + 1);

    o->type = OBJ_STRING;
    o->encoding = OBJ_ENCODING_EMBSTR;
    o->ptr = sh + 1;
    o->refcount = 1;

    sh->len = len;
    sh->alloc = len;
    sh->flags = SDS_TYPE_8;
    if (ptr == SDS_NOINIT)
        sh->buf[len] = '\0';
    else if (ptr) { // 将字符串内容复制到sds中
        memcpy(sh->buf, ptr, len);
        sh->buf[len] = '\0';
    } else  // 全部初始化为0
        memset(sh->buf, 0, len + 1);

    return o;
}

void trimStringObjectIfNeeded(robj* o) {
    // RAW编码，且剩余可用的空间 大于 已用空间的十分一
    if (o->encoding == OBJ_ENCODING_RAW && sdsavail(o->ptr) > sdslen(o->ptr) / 10)
        o->ptr = sdsRemoveFreeSpace(o->ptr);
}

// 尝试对对象进行编码
robj* tryObjectEncoding(robj* o) {
    long value;
    sds s = o->ptr;
    size_t len;

    assert(o->type == OBJ_STRING);


    // 是否是sds的编码类型，不是直接返回原来的对象
    if (!sdsEncodingObject(o)) return o;

    // 共享的对象，对其进行编码不安全（编码可能会释放原有的对象，创建新对象，如果原有的对象有别的引用就会有问题），所以不进行编码，直接返回原来的对象
    if (o->refcount > 1) return o;

    len = sdslen(s);
    // 如果给的value字符串可以转换成long类型的integer，同时value字符串的长度小于等于20
    if (len <= 20 && string2l(s, len, &value)) {
        // 共享已有的integer对象(server启动时候创建的)
        if ((server.maxmemory == 0 || !(server.maxmemory_policy & MAXMEMORY_FLAG_NO_SHARED_INTEGERS)) && value >= 0 &&
            value < OBJ_SHARED_INTEGERS) {
            decrRefCount(o);
            incrRefCount(shared.integers[value]);
            return shared.integers[value];
        } else {    // 不能共享已有的integer对象
            // 如果原对象是RAW编码的，改为INT编码的对象
            if (o->encoding == OBJ_ENCODING_RAW) {
                sdsfree(o->ptr);
                o->encoding == OBJ_ENCODING_INT;
                o->ptr = (void *) value;
                return o;
            } else if (o->encoding == OBJ_ENCODING_EMBSTR) {    // 如果原对象是EMBSTR编码的，重新创建字符串对象
                decrRefCount(o);

            }
        }
    }

    // 如果字符串还比较小，还可以用embstr编码
    // 就尝试创建embstr编码的字符串对象
    if (len <= OBJ_ENCODING_EMBSTR_SIZE_LIMIT) {
        robj* emb;

        if (o->encoding == OBJ_ENCODING_EMBSTR) return o;

        emb = createEmbeddedStringObject(s, sdslen(s));
        decrRefCount(o);
        return emb;
    }

    trimStringObjectIfNeeded(o);

    return o;
}

void freeStringObject(robj* o) {
    if (o->encoding == OBJ_ENCODING_RAW) {
        sdsfree(o->ptr);
    }
}

void freeListObject(robj* o) {
    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklistRelease(o->ptr);
    } else {
        panic("Unknown list encoding type");
    }
}

void freeSetObject(robj* o) {
    switch (o->encoding) {
        case OBJ_ENCODING_HT:
            dictRelease((dict*)o->ptr);
            break;
        case OBJ_ENCODING_INTSET:
            zfree(o->ptr);
            break;
        default:
            panic("Unknown set encoding type");
    }
}

void freeZsetObject(robj* o) {
    zset* zs;
    switch (o->encoding) {
        case OBJ_ENCODING_SKIPLIST:
            zs = o->ptr;
            dictRelease(zs->dict);
            zslFree(zs->zsl);
            zfree(zs);
            break;
        case OBJ_ENCODING_ZIPLIST:
            zfree(o->ptr);
        default:
            panic("Unknown sorted set encoding type");
    }
}

void freeHashObject(robj* o) {
    switch (o->encoding) {
        case OBJ_ENCODING_HT:
            dictRelease((dict*)o->ptr);
            break;
        case OBJ_ENCODING_ZIPLIST:
            zfree(o->ptr);
            break;
        default:
            panic("Unknown hash encoding type");
    }
}


void decrRefCount(robj* o) {
    if (o->refcount == 1) {
        switch (o->type) {
            case OBJ_STRING:
                freeStringObject(o);
                break;
            case OBJ_LIST:
                freeListObject(o);
                break;
            case OBJ_SET:
                freeSetObject(o);
                break;
            case OBJ_ZSET:
                freeZsetObject(o);
                break;
            case OBJ_HASH:
                freeHashObject(o);
                break;
            default:
                panic("Unknown object type");
        }
        zfree(o);
    } else {
        if (o->refcount <= 0) panic("decrRefCount against refcount <= 0");
        if (o->refcount != OBJ_SHARED_REFCOUNT) o->refcount--;
    }
}

void incrRefCount(robj* o) {
    if (o->refcount != OBJ_SHARED_REFCOUNT) o->refcount++;
}


robj* createObject(int type, void* ptr) {
    robj* o = zmalloc(sizeof(*o));
    o->type = type;
    o->encoding = OBJ_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;

    return o;
}

robj* createStringObjectFromLongLongWithOptions(long long value, int valueobj) {
    robj* o;

    if (server.maxmemory == 0 || !(server.maxmemory_policy & MAXMEMORY_FLAG_NO_SHARED_INTEGERS)) {
        valueobj = 0;
    }

    // 共享已有的integer对象
    if (value >= 0 && valueobj < OBJ_SHARED_INTEGERS && valueobj == 0) {
        incrRefCount(shared.integers[value]);
        o = shared.integers[value];
    } else {
        // long integer，创建INT编码的字符串对象
        if (value >= LONG_MIN && value <= LONG_MAX) {
            o = createObject(OBJ_STRING, NULL);
            o->encoding = OBJ_ENCODING_INT;
            o->ptr = (void *)((long)value);
        } else {    // 创建RAW编码的字符串对象
            o = createObject(OBJ_STRING, sdsfromlonglong(value));
            return createStringObjectFromLongLongForValue(value);
        }
    }



    return o;
}

// 尽可能的创建共享的对象
robj* createStringObjectFromLongLong(long long value) {
    return createStringObjectFromLongLongWithOptions(value, 0);
}

//
robj* createStringObjectFromLongLongForValue(long long value) {
    return createStringObjectFromLongLongWithOptions(value, 1);
}