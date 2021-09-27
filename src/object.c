//
// Created by xyzjiao on 9/22/21.
//

#include "server.h"

#define OBJ_ENCODING_EMBSTR_SIZE_LIMIT 44

robj* createStringObject(const char* ptr, size_t len) {
    if (len <= OBJ_ENCODING_EMBSTR_SIZE_LIMIT)
        return createEmbeddedStringObject(ptr, len);
    else
        return createRawStringObject(ptr, len);
}

robj* getDecodeObject(robj* o) {
    robj* dec;

    if (sdsEncodingObject(o)) {
        incrRefCount(o);
        return o;
    }

    // 如果是int编码的字符串对象，重新创建为embstr或str编码的字符串
    if (o->type == OBJ_STRING && o->encoding == OBJ_ENCODING_INT) {
        char buf[32];
        // OBJ_ENCODING_INT编码的对象，是将long类型的值存储在ptr变量里，用的时候需要将void*强转为long
        ll2string(buf, 32, (long)o->ptr);
        // 创建字符串对象
        dec = createStringObject(buf, strlen(buf));
        return dec;
    } else {
        panic("Unkown encoding type");
    }
}

// 创建raw编码的字符串对象
robj* createRawStringObject(const char* ptr, size_t len) {
    // 对象头和sds不是连续的空间，是两块空间，通过指针关联
    return createObject(OBJ_STRING, sdsnewlen(ptr, len));
}

// 创建embstr编码的字符串对象
robj* createEmbeddedStringObject(const char* ptr, size_t len) {
    // 分配一块连续的内存，包含对象头和sds的空间
    robj* o = zmalloc(sizeof(robj) + sizeof(struct sdshdr8) + len + 1);
    struct sdshdr8* sh = (void*) (o + 1);

    o->type = OBJ_STRING;
    o->encoding = OBJ_ENCODING_EMBSTR;
    o->ptr = sh + 1;    // 指向sds buf区域开头，即真正存储字符串的地方，跳过sds头部
    o->refcount = 1;

    if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
        o->lru = (LFUGetTimeInMinutes()<<8) | LFU_INIT_VAL;
    } else {
        o->lru = LRU_CLOCK();
    }

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

// 尝试对对象进行编码（所有存储在redis中的元素都会以字符串对象的形式存储）
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
                // 将整型value存储到ptr指针变量所在的内存空间，此时ptr变量不再是指针，而是一个整型。可以将指针当成整型来用
                o->ptr = (void *) value;
                return o;
            } else if (o->encoding == OBJ_ENCODING_EMBSTR) {    // 如果原对象是EMBSTR编码的，重新创建字符串对象
                decrRefCount(o);
                return createStringObjectFromLongLongForValue(value);
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

void decrRefCountVoid(void* o) {
    decrRefCount(o);
}

void incrRefCount(robj* o) {
    if (o->refcount != OBJ_SHARED_REFCOUNT) o->refcount++;
}

// 对于一个键值对来说，它的 LRU 时钟值最初是在这个键值对被创建的时候，进行初始化设置的，这个初始化操作是在 createObject 函数中调用的
robj* createObject(int type, void* ptr) {
    robj* o = zmalloc(sizeof(*o));
    o->type = type;
    o->encoding = OBJ_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;

    // 具体来说，就是如果 maxmemory_policy 配置为使用 LFU 策略，那么 lru 变量值会被初始化设置为 LFU 算法的计算值。
    // 而如果 maxmemory_policy 配置项没有使用 LFU 策略，那么，createObject 函数就会调用 LRU_CLOCK 函数来设置 lru 变量的值，也就是键值对对应的 LRU 时钟值。
    if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
        // 当 lru 变量用来记录 LFU 算法的所需信息时，它会用 24 bits 中的低 8 bits 作为计数器，来记录键值对的访问次数，同时它会用 24 bits 中的高 16 bits，记录访问的时间戳。
        // 第一部分是 lru 变量的高 16 位，是以 1 分钟为精度的 UNIX 时间戳。这是通过调用 LFUGetTimeInMinutes 函数（在 evict.c 文件中）计算得到的。
        // 第二部分是 lru 变量的低 8 位，被设置为宏定义 LFU_INIT_VAL（在server.h文件中），默认值为 5。
        o->lru = (LFUGetTimeInMinutes()<<8) | LFU_INIT_VAL;
    } else {
        // LRU_CLOCK 它的作用就是返回当前的全局 LRU 时钟值。因为一个键值对一旦被创建，也就相当于有了一次访问，所以它对应的 LRU 时钟值就表示了它的访问时间戳。
        o->lru = LRU_CLOCK();
    }

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

// 创建对象
robj* createStringObjectFromLongLongForValue(long long value) {
    return createStringObjectFromLongLongWithOptions(value, 1);
}

int getLongLongFromObject(robj* o, long long* target) {
    long long value;
    if (o == NULL) {
        value = 0;
    } else {
        assert(o->type == OBJ_STRING);
        if (sdsEncodingObject(o)) {
            if(string2ll(o->ptr, sdslen(o->ptr), &value) == 0) return C_ERR;
        } else if (o->encoding == OBJ_ENCODING_INT) {
            value = (long)o->ptr;
        } else {
            panic("Unknown string encoding");
        }
    }
    if (target) *target = value;
    return C_OK;
}

int getLongLongFromObjectOrReply(client* c, robj* o, long long* target, const char* msg) {
    long long value;

    if (getLongLongFromObject(o, &value) != C_OK) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c, "value is not an integer or out of range");
        }
        return C_ERR;
    }
    *target = value;
    return C_OK;
}



robj* createQuickllistObject(void) {
    quicklist* l = quicklistCreate();
    robj* o = createObject(OBJ_LIST, l);
    o->encoding = OBJ_ENCODING_QUICKLIST;
    return o;
}

// 检查对象的type是否是所需要的类型
int checkType(client* c, robj* o, int type) {
    if (o->type != type) {
        addReply(c, shared.wrongtypeerr);
        return 1;
    }
    return 0;
}

int getLongFromObjectOrReply(client* c, robj* o, long* target, const char* msg) {
    long long value;

    if (getLongLongFromObjectOrReply(c, o, &value, msg) != C_OK) return C_ERR;
    if (value < LONG_MIN || value > LONG_MAX) {
        if (msg != NULL) {
            addReplyError(c, (char*)msg);
        } else {
            addReplyError(c, "value is out of range");
        }
        return C_ERR;
    }
    *target = value;
    return C_OK;
}

robj* createHashObject(void) {
    unsigned char* zl = ziplistNew();
    robj* o = createObject(OBJ_HASH, zl);
    o->encoding = OBJ_ENCODING_ZIPLIST;
    return o;
}