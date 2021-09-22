//
// Created by xyzjiao on 9/9/21.
//

#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H
#include <limits.h>
#include "sds.h"
#include "dict.h"

#define MAXMEMORY_FLAG_LRU (1<<0)
#define MAXMEMORY_FLAG_LFU (1<<1)
#define MAXMEMORY_FLAG_ALLKEYS (1<2)
#define MAXMEMORY_FLAG_NO_SHARED_INTEGERS (MAXMEMORY_FLAG_LRU | MAXMEMORY_FLAG_LFU)

#define PROTO_SHARED_SELECT_CMDS 10
#define OBJ_SHARED_INTEGERS 10000
#define OBJ_SHARED_BULKHDR_LEN 32

#define UNIT_SECONDS 0
#define UNIT_MILLISECONDS 1

// 可以容纳2^64个元素
#define ZSKIPLIST_MAXLEVEL 64

#define ZSKIPLIST_P 0.25

#define LRU_BITS 24


#define OBJ_STRING 0
#define OBJ_LIST 1
#define OBJ_SET 2
#define OBJ_ZSET 3
#define OBJ_HASH 4

#define OBJ_ENCODING_RAW 0
#define OBJ_ENCODING_INT 1
#define OBJ_ENCODING_HT 2
#define OBJ_ENCODING_ZIPMAP 3
#define OBJ_ENCODING_LINKEDLIST 4
#define OBJ_ENCODING_ZIPLIST 5
#define OBJ_ENCODING_INTSET 6
#define OBJ_ENCODING_SKIPLIST 7
#define OBJ_ENCODING_EMBSTR 8
#define OBJ_ENCODING_QUICKLIST 9
#define OBJ_ENCODING_STREAM 10

#define OBJ_SHARED_REFCOUNT INT_MAX

typedef struct redisObject {
    unsigned type:4;    // 类型
    unsigned encoding:4;    // 编码
    unsigned lru:LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    int refcount;    // 该对象的引用数量
    void* ptr;  // 指向底层实现数据结构的指针
} robj;


typedef struct client {
    int argc;       // 命令的参数个数
    robj** argv;    // 命令的参数数组
} client;


struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
            *colon, *nullbulk, *nullmultibulk, *queued,
            *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
            *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
            *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
            *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
            *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *unlink,
            *rpop, *lpop, *lpush, *rpoplpush, *zpopmin, *zpopmax, *emptyscan,
            *select[PROTO_SHARED_SELECT_CMDS],
            *integers[OBJ_SHARED_INTEGERS],
            *mbulkhdr[OBJ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[OBJ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
    sds minstring, maxstring;
};

typedef struct redisServer {
    unsigned long long maxmemory;
    int maxmemory_policy;

};


typedef struct zskiplistNode {
    sds ele;    // 存储元素
    double score;   // 元素分值
    struct zskiplistNode* backward; // 后向指针(指向前一个节点，相邻的前面一个节点)
    struct zskiplistLevel {
        struct zskiplistNode* forward; // 指向后面的节点（不一定是相邻的后面一个节点，可能有跨度）
        unsigned long span; // 本节点  到forward指针指向的后面节点 的跨度
    } level[];  // 节点的level数组，保存每层上的前向指针(指向后一个节点)和跨度
} zskiplistNode;

typedef struct zskiplist {
    struct zskiplistNode* header, *tail;    // 指向跳表的表头和表尾节点
    unsigned long length;   // 跳表的长度，即跳表包含的节点数量（表头节点不计算在内）
    int level;  // 跳表最大层数
} zskiplist;

// 表示一个区间，min到max之间的，minex表示这个区间包不包含min，maxex表示这个区间包不包含max，为0表示包含，为1表示不包含
// minex = 1; maxex = 0; --> (min, max]
// minex = 0; maxex = 0; --> [min, max]
typedef struct {
    double min, max;
    int minex, maxex;
} zrangespec;

typedef struct {
    double min, max;
    int minex, maxex;
} zlexrangespec;

typedef struct zset {
    dict* dict;
    zskiplist* zsl;
} zset;


extern struct sharedObjectsStruct shared;
extern struct redisServer server;

zskiplistNode* zslCreateNode(int level, double score, sds ele);
zskiplist* zslCreate(void);
void zslFreeNode(zskiplistNode* node);
void zslFree(zskiplist* zsl);
zskiplistNode* zslInsert(zskiplist* zsl, double score, sds ele);
void zslDeleteNode(zskiplist* zsl, zskiplistNode* x, zskiplistNode** update);
int zslDelete(zskiplist* zsl, double score, sds els, zskiplistNode** node);
zskiplistNode* zslUpdateScore(zskiplist* zsl, double curscore, sds ele, double newscore);
unsigned long zslGetRank(zskiplist* zsl, double score, sds ele);
zskiplistNode* zslGetElementByRank(zskiplist* zsl, unsigned long rank);
int zslIsInRange(zskiplist* zsl, zrangespec* range);
int zslValueGteMin(double value, zrangespec* spec);
int zslValueLteMax(double value, zrangespec* spec);
zskiplistNode* zslFirstInRange(zskiplist* zsl, zrangespec* range);
zskiplistNode* zslLastInRange(zskiplist* zsl, zrangespec* range);


void setCommand(client *c);

robj* tryObjectEncoding(robj* o);
void decrRefCount(robj* o);
void incrRefCount(robj* o);
robj* createObject(int type, void* ptr);
robj* createStringObjectFromLongLong(long long value);
robj* createStringObjectFromLongLongForValue(long long value);
#define sdsEncodingObject(objptr) (objptr->encoding == OBJ_ENCODING_RAW || objptr->encoding == OBJ_ENCODING_EMBSTR)

#endif //REDIS_SERVER_H
