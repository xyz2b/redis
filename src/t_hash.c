//
// Created by xyzjiao on 9/23/21.
//

#include <ctype.h>
#include "server.h"


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
// 直接用传进来的field作为key；不然就复制一份（深copy），用复制的这份
#define HASH_SET_TAKE_FIELD (1<<0)
// 直接用传进来的value作为value；不然就复制一份（深copy），用复制的这份
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
        dictEntry* de = dictFind(o->ptr, field);
        if (de) {   // 已存在key，删除已有value，重新设置新的value
            // 删除已有value
            sdsfree(dictGetVal(de));

            // 设置新value
            if (flags & HASH_SET_TAKE_VALUE) {  // 直接使用传入的value
                dictGetVal(de) = value;
                value = NULL;
            } else {    // 复制一份新的value（深copy）
                dictGetVal(de) = sdsdup(value);
            }
            update = 1;
        } else { // key不存在，在字典中新增key/value
            sds f, v;
            if (flags & HASH_SET_TAKE_FIELD) {  // 直接使用传入的field
                f = field;
                field = NULL;
            } else { // 复制一份新的field（深copy）
                f = sdsdup(field);
            }

            if (flags & HASH_SET_TAKE_VALUE) {  // 直接使用传入的value
                v = value;
                value = NULL;
            } else {    // 复制一份新的value（深copy）
                v = sdsdup(value);
            }
            dictAdd(o->ptr, f, v);
        }
    } else {
        panic("Unknown hash encoding");
    }

    // 没看懂？
    if (flags & HASH_SET_TAKE_FIELD && field) sdsfree(field);
    if (flags & HASH_SET_TAKE_VALUE && value) sdsfree(value);
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
// key存在返回1，不存在返回0
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

    if ((o = hashTypeLookupWriteOrCreate(c, c->argv[1])) == NULL) return;

    // 根据键 和 值的长度判断是否需要转换编码，从ziplist转成hashtable
    // 根据键值对数量判断是否需要转换编码的代码，在hashTypeSet中
    hashTypeTryConversion(o, c->argv, 2, c->argc - 1);

    // 设置key/value，存在key就修改value，不存在就新增
    for (i = 2; i < c->argc; i += 2) {
        // hashTypeSet返回是否更新，更新了key的value就返回1，没有更新即代表新增了key/value就返回0
        created += !hashTypeSet(o, c->argv[i]->ptr, c->argv[i+1]->ptr, HASH_SET_COPY);
    }

    char* cmdname = c->argv[0]->ptr;
    if (cmdname[1] == 's' || cmdname[1] == 'S') {
        // HSET
        addReplyLongLong(c, created);   // 返回是新增了key还是更新了key，为1是新增key，为0是更新key
    } else {
        // HMSET
        addReply(c, shared.ok);
    }

    signalModifiedKey(c->db, c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH, "hset", c->argv[1], c->db->id);
    server.dirty++;
}


int hashTypeDelete(robj* o, sds field) {
    int deleted = 0;

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char* zl, *fptr;

        zl = o->ptr;
        fptr = ziplistIndex(zl, ZIPLIST_HEAD);
        if (fptr != NULL) { // zl不为空
            fptr = ziplistFind(fptr, (unsigned char*)field, sdslen(field), 1);
            if (fptr != NULL) { // 找到key
                zl = ziplistDelete(zl, &fptr);  // 删除key
                zl = ziplistDelete(zl, &fptr);  // 删除value
                o->ptr = zl;
                deleted = 1;
            }
        }
    } else if (o->encoding == OBJ_ENCODING_HT) {
        if (dictDelete((dict*)o->ptr, field) == C_OK) {
            deleted = 1;

            if (htNeedResize(o->ptr)) dictResize(o->ptr);
        }
    } else {
        panic("Unknown hash encoding");
    }
    return deleted;
}

// HDEL KEY_NAME FIELD1.. FIELDN
void hdelCommand(client* c) {
    robj* o;
    int j, deleted = 0, keyremoved = 0;

    if ((o = lookupKeyWriteOrReply(c, c->argv[1], shared.czero)) == NULL || checkType(c, o, OBJ_HASH)) return;

    for (j = 2; j < c->argc; j++) {
        if (hashTypeDelete(o, c->argv[j]->ptr)) {
            deleted++;
            // 删除的key对应的hash对象为空，直接从db中删除该key
            if (hashTypeLength(o) == 0) {
                dbDelete(c->db, c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }

    if (deleted) {
        signalModifiedKey(c->db, c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH, "hdel", c->argv[1], c->db->id);
        if (keyremoved) {
            notifyKeyspaceEvent(NOTIFY_HASH, "del", c->argv[1], c->db->id);
        }
        server.dirty += deleted;
    }
    addReplyLongLong(c, deleted);   // 返回删除了多少个元素
}

// HLEN KEY_NAME
void hlenCommand(client* c) {
    robj* o;

    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.czero)) == NULL || checkType(c, o, OBJ_HASH)) return;

    addReplyLongLong(c, hashTypeLength(o));
}

// 存在返回1，不存在返回0
int hashTypeExists(robj* o, sds field) {
    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        if (hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll) == 0) return 1;
    } else if (o->encoding == OBJ_ENCODING_HT) {
        if (hashTypeGetFromHashTable(o, field) != NULL) return 1;
    } else {
        panic("Unknown hash encoding");
    }
    return 0;
}

// HSETNX KEY_NAME FIELD VALUE
void hexitsCommand(client* c) {
    robj* o;
    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.czero)) == NULL || checkType(c, o, OBJ_HASH)) return;
    addReply(c, hashTypeExists(o, c->argv[2]->ptr) ? shared.cone : shared.czero);
}

// 将当前遍历节点(hi)的key/value作为结果返回
// 返回key还是value由what决定
static void addHashIteratorCursorToReply(client* c, hashTypeIterator* hi, int what) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);
        if (vstr) { // 字符串
            addReplyBulkBuffer(c, vstr, vlen);
        } else { // 整型
            addReplyBulkLongLong(c, vll);
        }
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        //
        sds value = hashTypeCurrentFromHashtable(hi, what);
        addReplyBulkBuffer(c, value, sdslen(value));
    } else {
        panic("Unknown hash encoding");
    }
}

// flags标识获取key还是value
void genericHgetallCommand(client* c, int flags) {
    robj* o;
    hashTypeIterator* hi;
    int multipiler = 0;
    int length, count = 0;

    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.emptymultibulk)) == NULL || checkType(c, o, OBJ_HASH)) return;

    if (flags & OBJ_HASH_KEY) multipiler++;
    if (flags & OBJ_HASH_VALUE) multipiler++;

    length = hashTypeLength(o) * multipiler;    // 总共获取的元素数量，key和value加一起的
    addReplyMultiBulkLen(c, length);    // 设置返回的数据数量

    hi = hashTypeInitIterator(o);
    while (hashTypeNext(hi) != C_ERR) {
        if (flags & OBJ_HASH_KEY) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_KEY);
            count++;
        }
        if (flags & OBJ_HASH_VALUE) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_VALUE);
            count++;
        }
    }

    hashTypeReleaseIterator(hi);
    assert(count == length);
}

void hkeysCommand(client* c) {
    genericHgetallCommand(c, OBJ_HASH_KEY);
}

void hvalsCommand(client* c) {
    genericHgetallCommand(c, OBJ_HASH_VALUE);
}

// HGETALL KEY_NAME
void hgetallCommand(client* c) {
    genericHgetallCommand(c, OBJ_HASH_KEY | OBJ_HASH_VALUE);
}

// check cursor是否有效，
// 命令中的cursor存在o中，有效参数返回解析出来的cursor
int parseScanCursorOrReply(client* c, robj* o, unsigned long* cursor) {
    char* eptr;

    errno = 0;

    // C 库函数 unsigned long int strtoul(const char *str, char **endptr, int base)
    // 把参数 str 所指向的字符串根据给定的 base 转换为一个无符号长整数（类型为 unsigned long int 型），
    // base 必须介于 2 和 36（包含）之间，或者是特殊值 0。
    /**
     *
        参数
            str -- 要转换为无符号长整数的字符串。
            endptr -- 对类型为 char* 的对象的引用，其值由函数设置为 str 中数值后的下一个字符。
            base -- 基数，必须介于 2 和 36（包含）之间，或者是特殊值 0。  (进制)

        返回值
            该函数返回转换后的长整数，如果没有执行有效的转换，则返回一个零值
     * */
    *cursor = strtoul(o->ptr, &eptr, 10);
    // C 库函数 int isspace(int c) 检查所传的字符是否是空白字符。如果 c 是一个空白字符，则该函数返回非零值（true），否则返回 0（false）
    // eptr不为空字符，正常将一个字符串解析完之后，eptr应该指向字符串结束标志符'\0'
    if (isspace(((char*)o->ptr)[0]) || eptr[0] != '\0' || errno == ERANGE) {
        addReplyError(c, "invalid cursor");
        return C_ERR;
    }
    return C_OK;
}


// HSCAN key cursor [MATCH pattern] [COUNT count]
// 根据匹配模式获取hash中匹配的key/value集合，然后迭代这些匹配出来的键值对
// cursor - 游标
// pattern - 匹配的模式
// count - 指定从数据集里返回多少元素，默认值为 10
void hscanCommand(client* c) {
    robj* o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c, c->argv[2], &cursor) == C_ERR) return;

    if ((o = lookupKeyReadOrReply(c, c->argv[1], shared.emptyscan)) == NULL || checkType(c, o, OBJ_HASH)) return;

    scanGenericCommand(c, o, cursor);
}