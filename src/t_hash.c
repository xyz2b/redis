//
// Created by xyzjiao on 9/23/21.
//

#include "server.h"
#include "redisassert.h"
#include "ziplist.h"
#include "sds.h"
#include "zmalloc.h"

void hashTypeConvert(robj* o, int enc);


unsigned long hashTypeLength(const robj* o) {
    unsigned long length = ULONG_MAX;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        length = ziplistLen(o->ptr) / 2;
    } else if (o->encoding == OBJ_ENCODING_HT) {
        length = dictSize((const dict*)o->ptr);
    } else {
        panic("Unknown hash encoding");
    }
    return length;
}

// hashTypeSet的flags值含义
// 直接用传进来的field作为key；不然就复制一份，用复制的这份，传进来的释放掉
#define HASH_SET_TAKE_FIELD (1<<0)
// 直接用传进来的value作为value；不然就复制一份，用复制的这份，传进来的释放掉
#define HASH_SET_TAKE_VALUE (1<<1)
#define HASH_SET_COPY 0

// 在hash对象中set key/value
// 其中会根据键值对数量判断是否需要转换编码
// 发生了更新返回1（key已存在），没有发生更新返回0（key不存在）
int hashTypeSet(robj* o, sds field, sds value, int flags) {
    int update = 0;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char* zl, *fptr, *vptr;

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) {
            // 找到key所在ziplist中的项
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) { // 在ziplist找到了key
                // 获取value所在的项
                vptr = ziplistNext(zl, fptr);

                assert(vptr != NULL);
                update = 1;

                // 删除现有的value
                ziplistDelete(zl, &vptr);

                // 在原来value的位置，重新插入新的value
                zl = ziplistInsert(zl, vptr, (unsigned char*) value, sdslen(value));
            }
        }

        // ziplist中没找到key，在尾部插入
        if (!update) {
            zl = ziplistPush(zl, (unsigned char*)field, sdslen(field), ZIPLIST_TAIL);
            zl = ziplistPush(zl, (unsigned char*)value, sdslen(value), ZIPLIST_TAIL);
        }

        // 因为再插入过程中，可能涉及内存重新分配，原zl的地址可能会发生改变，所以这里需要重新赋值
        o->ptr = zl;

        // 如果插入之后现有的键值对数量大于设置的ziplist所能存储的最大值，就需要转换编码为hashtable
        if (hashTypeLength(o) > server.hash_max_ziplist_entries)
            hashTypeConvert(o, OBJ_ENCODING_HT);
    } else if (o->encoding == OBJ_ENCODING_HT) {

    } else {
        panic("Unknown hash encoding");
    }
    return update;
}

// filed作为key，从hashtable作为底层结构的hash中（o）获取值
// 以sds字符串形式返回
sds hashTypeGetFromHashTable(robj* o, sds field) {
    dictEntry* de;

    assert(o->encoding == OBJ_ENCODING_HT);

    de = dictFind(o->ptr, field);
    if (de == NULL) return NULL;
    return dictGetVal(de);
}

// filed作为key，从ziplist作为底层结构的hash中（o）获取值
// 字符串型值：vstr返回值，vlen返回值的长度
// 整型值：vll返回值
int hashTypeGetFromZiplist(robj* o, sds field, unsigned char** vstr, unsigned int* vlen, long long* vll) {
    unsigned char* zl, *fptr = NULL, *vptr = NULL;
    int ret;

    assert(o->encoding == OBJ_ENCODING_ZIPLIST);

    // 获取ziplist
    zl = o->ptr;
    // 获取ziplist第一项
    fptr = ziplistIndex(zl, ZIPLIST_HEAD);
    if (fptr != NULL) {
        // 在ziplist中从第一项往后查找，与filed值相同的项（找key）
        fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
        if (fptr != NULL) { // key存在
            // 获取value的项，就在存储key的项后面一项
            vptr = ziplistNext(zl, fptr);
            assert(vptr != NULL);
        }
    }

    if (vptr != NULL) {
        // 获取value的值
        ret = ziplistGet(vptr, vstr, vlen, vll);
        assert(ret);
        return 0;
    }

    return -1;
}

// o是hash对象的底层结构，为ziplist或ht，filed是hash的key
static void addHashFieldToReply(client* c, robj* o, sds field) {
    int ret;

    if (o == NULL) {
        addReply(c, shared.nullbulk);
        return;
    }

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char* vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        ret = hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll);
        if (ret < 0) {  // 没找到key
            addReply(c, shared.nullbulk);
        } else {
            if (vstr) { // 字符串
                addReplyBulkBuffer(c, vstr, vlen);
            } else {    // 整型
                addReplyLongLong(c, vll);
            }
        }
    } else if (o->encoding == OBJ_ENCODING_HT) {
        sds value = hashTypeGetFromHashTable(o, field);
        if (value == NULL)
            addReply(c, shared.nullbulk);
        else
            addReplyBulkBuffer(c, value, sdslen(value));
    } else {
        panic("Unknown hash encoding");
    }
}

// HGET KEY_NAME FIELD_NAME
void hgetCommand(client* c) {
    robj* o;

    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk)) == NULL || checkType(c, o, OBJ_HASH)) return;

    addHashFieldToReply(c, o, c->argv[2]->ptr);
}

// HMGET KEY_NAME FIELD1...FIELDN
void hmgetCommand(client* c) {
    robj* o;
    int i;

    o = lookupKeyRead(c->db, c->argv[1]);
    if (o != NULL && o->type != OBJ_HASH) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    // 因为要返回多个数据，需要先设置下发送的数据个数
    addReplyMultiBulkLen(c, c->argc-2);
    for (i = 2; i < c->argc; i++) {
        addHashFieldToReply(c, o, c->argv[i]->ptr);
    }
}

// 在db中查询key是否存在，不存在就创建，存在就返回底层的数据结构
robj* hashTypeLookupWriteOrCreate(client* c, robj* key){
    // 在db中查找key
    robj* o = lookupKeyWrite(c->db, key);
    if (o == NULL) {    // db中不存在key
        o = createHashObject();
        dbAdd(c->db, key, o);
    } else {
        if (o->type != OBJ_HASH) {
            addReply(c, shared.wrongtypeerr);
            return NULL;
        }
    }
    return o;
}

hashTypeIterator* hashTypeInitIterator(robj* subject) {
    hashTypeIterator* hi = zmalloc(sizeof(hashTypeIterator));
    hi->subject = subject;
    hi->encoding = subject->encoding;

    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        hi->fptr = NULL;
        hi->vptr = NULL;
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        hi->di = dictGetIterator(subject->ptr);
    } else {
        panic("Unknown hash encoding");
    }
    return hi;
}

int hashTypeNext(hashTypeIterator* hi) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char* zl;
        unsigned char* fptr, *vptr;

        zl = hi->subject->ptr;
        fptr = hi->fptr;
        vptr = hi->vptr;

        if (fptr == NULL) {
            // 初始化迭代器
            assert(vptr == NULL);
            fptr = ziplistIndex(zl, 0);
        } else {
            assert(vptr != NULL);
            // 当前value项的下一项就是下一对key/value的key的项
            fptr = ziplistNext(zl, vptr);
        }
        if (fptr == NULL) return C_ERR;

        // 获取下一个key/value的value的项
        vptr = ziplistNext(zl, fptr);
        assert(vptr != NULL);

        hi->fptr = fptr;
        hi->vptr = vptr;
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        if ((hi->de = dictNext(hi->di)) == NULL) return C_ERR;
    } else {
        panic("Unknown hash encoding");
    }
    return C_OK;
}

// 从hash对象底层ziplist中获取key/value的值
// hi: hash对象的迭代器
// what: 获取的是key还是value
// vstr: 返回字符串型的值
// vlen: 返回的字符串型的值的长度
// vll: 返回的整型的值
void hashTypeCurrentFromZiplist(hashTypeIterator* hi, int what, unsigned char** vstr, unsigned int* vlen, long long* vll) {
    int ret;

    assert(hi->encoding == OBJ_ENCODING_ZIPLIST);

    if (what & OBJ_HASH_KEY) {
        ret = ziplistGet(hi->fptr, vstr, vlen, vll);
        assert(ret);
    } else {
        ret = ziplistGet(hi->vptr, vstr, vlen, vll);
        assert(ret);
    }
}

// 从hash对象底层hashtable中获取key/value的值
// hi: hash对象的迭代器
// what: 获取的是key还是value
// 返回key/value的值
sds hashTypeCurrentFromHashtable(hashTypeIterator* hi, int what) {
    assert(hi->encoding == OBJ_ENCODING_HT);

    if (what & OBJ_HASH_KEY) {
        return dictGetKey(hi->de);
    } else {
        return dictGetVal(hi->de);
    }
}

// 从hash对象中获取key/value的值
// hi: hash对象的迭代器
// what: 获取的是key还是value
// vstr: 返回字符串型的值
// vlen: 返回的字符串型的值的长度
// vll: 返回的整型的值
void hashTypeCurrentObject(hashTypeIterator* hi, int what, unsigned char** vstr, unsigned int* vlen, long long* vll) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        *vstr = NULL;
        hashTypeCurrentFromZiplist(hi, what, vstr, vlen, vll);
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        sds ele = hashTypeCurrentFromHashtable(hi, what);
        *vstr = (unsigned char*) ele;
        *vlen = sdslen(ele);
    } else {
        panic("Unknown hash encoding");
    }
}

// 从hash对象中获取key/value的值，并将它们包装成sds返回
// hi: hash对象的迭代器
// what: 获取的是key还是value
sds hashTypeCurrentObjectNewSds(hashTypeIterator* hi, int what) {
    unsigned char* vstr;
    unsigned int vlen;
    long long vll;

    hashTypeCurrentObject(hi, what, &vstr, &vlen, &vll);
    if (vstr) return sdsnewlen(vstr, vlen);     // 字符串型值
    return sdsfromlonglong(vll);    // 整型值
}

void hashTypeReleaseIterator(hashTypeIterator* hi) {
    if (hi->encoding == OBJ_ENCODING_HT)
        dictReleaseIterator(hi->di);
    zfree(hi);
}

// hash对象底层从ziplist转成hashtable
void hashTypeConvertZiplist(robj* o, int enc) {
    assert(o->encoding == OBJ_ENCODING_ZIPLIST);

    if (enc == OBJ_ENCODING_ZIPLIST) {
        // nothing to do
    } else if (enc == OBJ_ENCODING_HT) {
        hashTypeIterator* hi;
        dict* dict;
        int ret;

        hi = hashTypeInitIterator(o);
        // 创建dict
        dict = dictCreate(&hashDictType, NULL);

        // 遍历ziplist中的每个key/value，将其取出来添加到上面创建的dict中
        while (hashTypeNext(hi) != C_ERR) {
            sds key, value;

            // 从hash对象中取出当前遍历的key/value并将他们创建为sds
            key = hashTypeCurrentObjectNewSds(hi, OBJ_HASH_KEY);
            value = hashTypeCurrentObjectNewSds(hi, OBJ_HASH_VALUE);
            // 将取出的key/value存入hashtable中
            ret = dictAdd(dict, key, value);

            if (ret != DICT_OK) {
                panic("Ziplist corruption detected");
            }
        }
        hashTypeReleaseIterator(hi);
        zfree(o->ptr);
        o->encoding = OBJ_ENCODING_HT;
        o->ptr = dict;
    } else {
        panic("Unknown hash encoding");
    }
}

// 转换hash对象的编码，改变底层数据结构
void hashTypeConvert(robj* o, int enc) {
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        hashTypeConvertZiplist(o, enc);
    } else if (o->encoding == OBJ_ENCODING_HT) {
        panic("Not implemented");
    } else {
        panic("Unknown hash encoding");
    }
}

// 根据键 和 值的长度判断是否需要转换编码，从ziplist转成hashtable
void hashTypeTryConversion(robj* o, robj** argv, int start, int end) {
    int i;

    if (o->encoding != OBJ_ENCODING_ZIPLIST) return;

    for (i = start; i <= end; i++) {
        if (sdsEncodingObject(argv[i]) && sdslen(argv[i]->ptr) > server.hash_max_ziplist_value) {
            hashTypeConvert(o, OBJ_ENCODING_HT);
            break;
        }
    }
}


// HSET KEY_NAME FIELD VALUE
// HMSET KEY_NAME FIELD1 VALUE1 ...FIELDN VALUEN
void hsetCommand(client* c) {
    int i, created = 0;
    robj* o;

    if ((c->argc % 2) == 1) {
        addReplyError(c, "wrong number of argumnets for HMSET");
        return;
    }

    if ((o == hashTypeLookupWriteOrCreate(c, c->argv[1])) == NULL) return;

    // 根据键 和 值的长度判断是否需要转换编码，从ziplist转成hashtable
    // 根据键值对数量判断是否需要转换编码的代码，在hashTypeSet中
    hashTypeTryConversion(o, c->argv, 2, c->argc - 1);

    for (i = 2; i < c->argc; i += 2) {
        created += !hashTypeSet(o, c->argv[i]->ptr, c->argv[i+1]->ptr, HASH_SET_COPY);
    }
}