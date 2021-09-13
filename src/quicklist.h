//
// Created by xyzjiao on 9/12/21.
//

#ifndef REDIS_QUICKLIST_H
#define REDIS_QUICKLIST_H
#include <stddef.h>

#define SIZE_SAFETY_LIMIT 8192

static const size_t optimization_level[] = {4096, 8192, 16384, 32768, 65536};

typedef struct quicklistNode {
    struct quicklistNode* prev; // 前一个quicklistNode
    struct quicklistNode* next; // 后一个quicklistNode
    unsigned char* zl;  // quicklistNode指向的ziplist
    unsigned int sz;    // ziplist的字节大小
    unsigned int count : 16;    // ziplist中的元素个数
    unsigned int encoding : 2;  // 编码格式，原生字节数组或压缩存储
    unsigned int container : 2; // 存储方式，ziplist
    unsigned int recompress : 1;    // 数据是否被压缩
    unsigned int attempted_compress : 1;    // 数据能否压缩
    unsigned int extra : 10;    // 预留的bit位
} quicklistNode;


typedef struct quicklist {
    quicklistNode *head;        // quicklist链表头
    quicklistNode *tail;        // quicklist链表尾
    unsigned long count;        // 所有ziplist中的总元素个数
    unsigned long len;          // quicklistNode的个数
    // 用于决定对于某个node，新增元素时，是直接在该node的ziplist中插入，还是新增一个node来存储，从而来控制单个node中的ziplist的大小，从而控制连锁更新的范围
    // 如果fill为负数，则是限制ziplist的长度
    // 如果file为正数，则是限制ziplist中的元素个数
    int fill : 16;              // 单个节点的填充因子。
    unsigned int compress : 16; // 未压缩的末端节点深度；0=关闭
} quicklist;


#define QUCIKLIST_NODE_ENCODING_RAW 1
#define QUCIKLIST_NODE_ENCODING_LZF 2

#define QUCIKLIST_NODE_CONITAINER_NONE 1
#define QUCIKLIST_NODE_CONITAINER_ZIPLIST 1


quicklist* quicklistCreate(void);
quicklist* quciklistNew(int fill, int compress);

int quicklistPushHead(quicklist* quicklist, void* value, size_t sz);
int quicklistPushTail(quicklist* quicklist, void* value, size_t sz);
void quicklistPush(quicklist* quicklist, void* value, size_t sz, int where);

#endif //REDIS_QUICKLIST_H
