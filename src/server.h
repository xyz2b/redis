//
// Created by xyzjiao on 9/9/21.
//

#ifndef REDIS_SERVER_H
#define REDIS_SERVER_H
#include <limits.h>
#include "sds.h"
#include "dict.h"
#include "adlist.h"
#include "quicklist.h"

#define C_OK 0
#define C_ERR -1

#define CMD_READONLY (1<<1)

typedef long long mstime_t;
typedef long long ustime_t;

#define LIST_HEAD 0
#define LIST_TAIL 1


#define NOTIFY_STRING (1<<3)        // $
#define NOTIFY_GENERIC (1<<2)       // g
#define NOTIFY_LIST (1<<4)          // l


#define CLIENT_DIRTY_CAS (1<<5)

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
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1)
#define LRU_CLOCK_RESOLUTION 1000   // 它表示的是以毫秒为单位的 LRU 时钟精度，也就是以毫秒为单位来表示的 LRU 时钟最小单位。
// 因为 LRU_CLOCK_RESOLUTION 的默认值是 1000，所以，LRU 时钟精度就是 1000 毫秒，也就是 1 秒。
// 这样一来，你需要注意的就是，如果一个数据前后两次访问的时间间隔小于 1 秒，那么这两次访问的时间戳就是一样的。因为 LRU 时钟的精度就是 1 秒，它无法区分间隔小于 1 秒的不同时间戳。

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

typedef struct redisDb {
    dict* dict;     // keyspace
    dict* expires;
    dict* blocking_keys;
    dict* ready_keys;
    dict* watch_keys;   // 被客户端watch的key的字典，字典的key为被watch的key，value为watch该key的client链表
    int id;     // 标识是几号db
    long long avg_ttl;
    list* defrag_later;
} redisDb;

typedef struct redisObject {
    unsigned type:4;    // 类型
    unsigned encoding:4;    // 编码
    unsigned lru:LRU_BITS; //记录LRU信息，宏定义LRU_BITS是24 bits
                            /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    int refcount;    // 该对象的引用数量
    void* ptr;  // 指向底层实现数据结构的指针
} robj;


struct redusCommand {
    char *name;
    int flags;
};


typedef struct client {
    redisDb* db;    // 当前选择的db（如db0，db1等）

    int argc;       // 命令的参数个数
    robj** argv;    // 命令的参数数组

    struct redusCommand* cmd;

    int flags;

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
    // 一旦我们设定了 maxmemory 选项（不为0），并且将 maxmemory-policy 配置为 allkeys-lru 或是 volatile-lru 时，近似 LRU 算法就被启用了。
    // 这里，你需要注意的是，allkeys-lru 和 volatile-lru 都会使用近似 LRU 算法来淘汰数据，
    // 它们的区别在于：采用 allkeys-lru 策略淘汰数据时，它是在所有的键值对中筛选将被淘汰的数据；
    // 而采用 volatile-lru 策略淘汰数据时，它是在设置了过期时间的键值对中筛选将被淘汰的数据
    unsigned long long maxmemory;   // 该配置项设定了 Redis server 可以使用的最大内存容量，一旦 server 使用的实际内存量超出该阈值时，server 就会根据 maxmemory-policy 配置项定义的策略，执行内存淘汰操作（为0，表示不限制内存使用）
    int maxmemory_policy;   // 该配置项设定了 Redis server 的内存淘汰策略，主要包括近似 LRU 算法、LFU 算法、按 TTL 值淘汰和随机淘汰等几种算法
    time_t unixtime;    // 当前时间
    int hz; // serverCron执行的频率，hz 配置项的默认值是 10，这表示 serverCron 函数会每 100 毫秒（1 秒 /10 = 100 毫秒）运行一次
    unsigned int lruclock;  // 全局 LRU 时钟
    int lazyfree_lazy_server_del;
    pthread_mutex_t lruclock_mutex; // 锁


    pid_t rdb_child_pid;    // 做rdb的子进程id
    pid_t aof_child_pid;    // 做aof的子进程id


    long long dirty;

    client* curren_client;
    client* master;
    char* masterhost;

    int cluster_enabled;


    long long stat_keyspace_misses;
    long long stat_keyspace_hists;


    int list_max_ziplist_size;
    int list_compress_depth;
};

// list迭代器，包装了quicklist的迭代器，因为list的底层就是quicklist
typedef struct {
    robj* subject;
    unsigned char encoding;
    unsigned char direction;
    quicklistIter* iter;
} listTypeIterator;

// list迭代器的entry
typedef struct {
    listTypeIterator* li;
    quicklistEntry entry;
} listTypeEntry;

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

// 字符串对象
void setCommand(client *c);
void setnxCommand(client* c);
void setexCommand(client* c);
void psetexCommand(client* c);
void getCommand(client* c);
void getsetCommand(client* c);

// list对象
void lpushCommand(client* c);
void rpushCommand(client* c);
void lpopCommand(client* c);
void rpopCommand(client* c);

        robj* tryObjectEncoding(robj* o);
void decrRefCount(robj* o);
void incrRefCount(robj* o);
robj* createObject(int type, void* ptr);
robj* createStringObjectFromLongLong(long long value);
robj* createStringObjectFromLongLongForValue(long long value);
unsigned long LFUGetTimeInMinutes(void);
mstime_t mstime(void);
long long ustime(void);
unsigned int LRU_CLOCK(void);
int getLongLongFromObjectOrReply(client* c, robj* o, long long* target, const char* msg);
robj* lookupKeyWrite(redisDb* db, robj* key);
int expireIfNeeded(redisDb* db, robj* key);
robj* lookupKey(redisDb* db, robj* key, int flags);
void setKey(redisDb* db, robj* key, robj* val);
void signalKeyAsReady(redisDb* db, robj* key);
void slotToKeyAdd(robj* key);
void freeObjAsync(robj* o);
void setExpire(client* c, redisDb* db, robj* key, long long when);
void notifyKeyspaceEvent(int type, char* event, robj* key, int dbid);
robj* lookupKeyReadOrReply(client *c, robj* key, robj* reply);
robj* createQuickllistObject(void);
void dbAdd(redisDb* db, robj* key, robj* val);
robj* createEmbeddedStringObject(const char* ptr, size_t len);
robj* createRawStringObject(const char* ptr, size_t len);
robj* getDecodeObject(robj* o);
void addReply(client* c, robj* obj);
void addReplyError(client* c, const char* err);
void addReplyErrorFormat(client* c, const char* fmt, ...);
void addReplyLongLong(client* c, long long ll);
void addReplyBulk(client* c, robj* obj);
robj* lookupKeyWriteOrReply(client* c, robj* key, robj* reply);
void signalModifiedKey(redisDb* db, robj* key);
void touchWatchKey(redisDb* db, robj* key);
int checkType(client* c, robj* o, int type);
robj* createStringObject(const char* ptr, size_t len);
int dbDelete(redisDb* db, robj* key);
#define LOOKUP_NONE 0
#define LOOKUP_NOTOUCH (1<<0)
#define sdsEncodingObject(objptr) (objptr->encoding == OBJ_ENCODING_RAW || objptr->encoding == OBJ_ENCODING_EMBSTR)
#define LFU_INIT_VAL 5

#endif //REDIS_SERVER_H
