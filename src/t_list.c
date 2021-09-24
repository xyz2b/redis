//
// Created by xyzjiao on 9/23/21.
//

#include <string.h>
#include "server.h"
#include "quicklist.h"
#include "redisassert.h"
#include "zmalloc.h"


void* listPopSaver(unsigned char* data, unsigned int sz) {
    return createStringObject((char*)data, sz);
}

// 从quiclist中pop一个元素，以字符串对象形式返回
robj* listTypePop(robj* subject, int where) {
    long long vlong;
    robj* value = NULL;

    int ql_where = where == LIST_HEAD ? QUICKLIST_HEAD : QUICKLIST_TAIL;
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        // 如果pop出来的是字符串类型的值，则用自定义的函数，将quicklist pop出来的元素包装成字符串对象，然后用value返回；如果pop出来的值是整型类型的值，则直接用vlong返回
        if (quicklistPopCustom(subject->ptr, ql_where, (unsigned char **)&value, NULL, &vlong, listPopSaver)) {
            if (!value) // 整型类型的值，也将其包装为字符串对象
                value = createStringObjectFromLongLong(vlong);
        }
    } else {
        panic("Unknown list encoding");
    }
    return value;
}

// subject: quicklist的对象
// value: 要压入quicklist的元素对象
// where: 压入的位置
void listTypePush(robj* subject, robj* value, int where) {
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        int pos = (where == LIST_HEAD) ? QUICKLIST_HEAD : QUICKLIST_TAIL;
        // 重新编码要压入quicklist的元素，将int编码的转成embstr或str编码的字符串，因为quicklist存储的是真实字符串，而int编码对象的ptr直接存储了一个整型数字，不是字符串指针
        value = getDecodeObject(value->ptr);
        size_t len = sdslen(value->ptr);
        // quicklist存储的是真实字符串的指针
        quicklistPush(subject->ptr, value->ptr, len, pos);
        decrRefCount(value);
    } else {
        panic("Unknown list encoding");
    }
}

unsigned long listTypeLength(const robj* subject) {
    if (subject->encoding == OBJ_ENCODING_QUICKLIST) {
        return quicklistCount(subject->ptr);
    } else {
        panic("Unknown list encoding");
    }
}

void pushGenericCommand(client* c, int where) {
    int j, pushed = 0;
    // 在db的keyspace中查找key是否存在
    robj* lobj = lookupKeyWrite(c->db, c->argv[1]);


    if (lobj && lobj->type != OBJ_LIST) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    // 支持多个值批量压入
    for (j = 2; j < c->argc; j++) {
        // 对应的key不存在，在db中新增key，同时创建底层的quicklist数据结构，作为该key的value
        if (!lobj) {
            lobj = createQuickllistObject();
            quicklistSetOptions(lobj->ptr, server.list_max_ziplist_size, server.list_compress_depth);
            dbAdd(c->db, c->argv[1], lobj);
        }

        // 向quicklist中压入value
        listTypePush(lobj, c->argv[j], where);
        pushed++;
    }

    // 返回list当前的元素个数
    addReplyLongLong(c, (lobj ? listTypeLength(lobj) : 0));
    if (pushed) {
        char* event = (where == LIST_HEAD) ? "lpush" : "rpush";
        // 触发key对应的value被修改的信号，给所有watch该key的client
        signalModifiedKey(c->db, c->argv[1]);
        // 触发事件
        notifyKeyspaceEvent(NOTIFY_LIST, event, c->argv[1], c->db->id);
    }
    server.dirty += pushed;
}

// LPUSH KEY_NAME VALUE1..VALUEN
void lpushCommand(client* c) {
    pushGenericCommand(c, LIST_HEAD);
}

// RPUSH KEY_NAME VALUE1..VALUEN
void rpushCommand(client* c) {
    pushGenericCommand(c, LIST_TAIL);
}

void popGenericCommand(client* c, int where) {
    // 获取key对应的quicklist对象
    robj* o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk);
    if (o == NULL || checkType(c, o, OBJ_LIST)) return;

    // 从quicklist对象中pop一个元素出来
    robj* value = listTypePop(o, where);
    if (value == NULL) {
        addReply(c, shared.nullbulk);
    } else {
        char* event = (where == LIST_HEAD) ? "lpop" : "rpop";

        addReplyBulk(c, value);
        decrRefCount(value);
        notifyKeyspaceEvent(NOTIFY_LIST, event, c->argv[1], c->db->id);
        if (listTypeLength(o) == 0) {   // quicklist为空，就从db中删除该quicklist的key
            notifyKeyspaceEvent(NOTIFY_GENERIC, "del", c->argv[1], c->db->id);
            dbDelete(c->db, c->argv[1]);
        }
        signalModifiedKey(c->db, c->argv[1]);
        server.dirty++;
    }
}

// LPOP KEY_NAME
void lpopCommand(client* c) {
    popGenericCommand(c, LIST_HEAD);
}

// RPOP KEY_NAME
void rpopCommand(client* c) {
    popGenericCommand(c, LIST_TAIL);
}

void listTypeReleaseIterator(listTypeIterator* li) {
    zfree(li->iter);
    zfree(li);
}

listTypeIterator* listTypeInitIterator(robj* subject, long index, unsigned char direction) {
    listTypeIterator* li = zmalloc(sizeof(listTypeIterator));
    li->subject = subject;
    li->encoding = subject->encoding;
    li->direction = direction;
    li->iter = NULL;

    int iter_direction = direction == LIST_HEAD ? AL_START_TAIL : AL_START_HEAD;
    if (li->encoding == OBJ_ENCODING_QUICKLIST) {
        li->iter = quicklistGetIteratorAtIdx(li->subject->ptr, iter_direction, index);
    } else {
        panic("Unknown list encoding");
    }
    return li;
}

int listTypeNext(listTypeIterator* li, listTypeEntry* entry) {
    assert(li->subject->encoding == li->encoding);

    entry->li = li;
    if (li->encoding == OBJ_ENCODING_QUICKLIST) {
        return quicklistNext(li->iter, &entry->entry);
    } else {
        panic("Unknown list encoding");
    }
    return 0;
}

int listTypeEqual(listTypeEntry* entry, robj* o) {
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        // 比较的对象必须是sds字符串
        assert(sdsEncodingObject(o));
        return quicklistCompare(entry->entry.zi, o->ptr, sdslen(o->ptr));
    } else {
        panic("Unknown list encoding");
    }
}

// 从list迭代器的entry中获取对应的元素，以字符串对象形式返回
robj* listTypeGet(listTypeEntry* entry) {
    robj* value = NULL;
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        if (entry->entry.value) {   // 字符串型值
            value = createStringObject((char*)entry->entry.value, entry->entry.sz);
        } else {    // 整型值
            value = createStringObjectFromLongLong(entry->entry.longval);
        }
    } else {
        panic("Unknown list encoding");
    }
    return value;
}

void listTypeInsert(listTypeEntry* entry, robj* value, int where) {
    if (entry->li->encoding == OBJ_ENCODING_QUICKLIST) {
        value = getDecodeObject(value);
        sds str = value->ptr;
        size_t len = sdslen(str);
        if (where == LIST_TAIL) {
            quicklistInsertAfter((quicklist*)entry->entry.quicklist, &entry->entry, str, len);
        } else if (where == LIST_HEAD) {
            quicklistInsertBefore((quicklist*)entry->entry.quicklist, &entry->entry, str, len);
        }
        decrRefCount(value);
    } else {
        panic("Unknown list encoding");
    }
}

// LINSERT key BEFORE|AFTER pivot value
// 将值 value 插入到列表 key 当中，位于值 pivot 之前或之后
void linsertCommand(client* c) {
    int where;
    robj* subject;
    listTypeIterator* iter;
    listTypeEntry entry;
    int inserted = 0;

    if (strcasecmp(c->argv[2]->ptr, "after") == 0) {
        where = LIST_TAIL;
    } else if (strcasecmp(c->argv[2]->ptr, "before") == 0) {
        where = LIST_HEAD;
    } else {
        addReply(c, shared.syntaxerr);
        return;
    }

    if ((subject == lookupKeyWriteOrReply(c, c->argv[1], shared.czero)) == NULL || checkType(c, subject, OBJ_LIST)) return;

    // 从头到尾遍历，找到插入位置，并插入
    iter = listTypeInitIterator(subject, 0, LIST_TAIL);
    while (listTypeNext(iter, &entry)) {
        if (listTypeEqual(&entry, c->argv[3])) {
            listTypeInsert(&entry, c->argv[4], where);
            inserted = 1;
            break;
        }
    }
    listTypeReleaseIterator(iter);

    // 如果插入成功就创建对应的信号和事件
    if (inserted) {
        signalModifiedKey(c->db, c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_LIST, "linsert", c->argv[1], c->db->id);
        server.dirty++;
    } else {
        addReply(c, shared.cnegone);
        return;
    }
    // 返回插入后list的元素个数
    addReplyLongLong(c, listTypeLength(subject));
}

// LLEN KEY_NAME
void llenCommand(client* c) {
    robj* o = lookupKeyReadOrReply(c, c->argv[1], shared.czero);
    if (o == NULL || checkType(c, o, OBJ_LIST)) return;
    addReplyLongLong(c, listTypeLength(o));
}

// LINDEX KEY_NAME INDEX_POSITION
// 列表中下标为指定索引值的元素。 如果指定索引值不在列表的区间范围内，返回 nil
void lindexCommand(client* c) {
    robj* o = lookupKeyReadOrReply(c, c->argv[1], shared.nullbulk);
    if (o == NULL || checkType(c, o, OBJ_LIST)) return;
    long index;
    robj* value = NULL;

    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != C_OK))
        return;

    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklistEntry entry;
        if (quicklistIndex(o->ptr, index, &entry)) {
            if (entry.value) {  // 字符串型值
                value = createStringObject((char*)entry.value, entry.sz);
            } else {    // 整型值
                value = createStringObjectFromLongLong(entry.longval);
            }
            addReplyBulk(c, value);
            // value只是用于临时存储查询到的值的对象，在返回结果之后，value对象就没有用了，需要将其引用减一（不直接删除的原因是 value在创建时可能是共享的，如果不睡共享的，引用为0，也会被回收）
            decrRefCount(value);
        } else {
            addReply(c, shared.nullbulk);
        }
    } else {
        panic("Unknown list encoding");
    }
}

// LSET KEY_NAME INDEX VALUE
void lsetCommand(client* c) {
    robj* o = lookupKeyWriteOrReply(c, c->argv[1], shared.nokeyerr);
    if (o == NULL || checkType(c, o, OBJ_LIST)) return;
    long index;
    robj* value = c->argv[3];

    if ((getLongFromObjectOrReply(c, c->argv[2], &index, NULL) != C_OK)) return;

    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklist* ql = o->ptr;
        int replaced = quicklistReplaceAtIndex(ql, index, value->ptr, sdslen(value->ptr));

        if (!replaced) {
            addReply(c, shared.outofrangeerr);
        } else {
            addReply(c, shared.ok);
            signalModifiedKey(c->db, c->argv[1]);
            notifyKeyspaceEvent(NOTIFY_LIST, "lset", c->argv[1], c->db->id);
            server.dirty++;
        }
    } else {
        panic("Unknown list encoding");
    }
}
