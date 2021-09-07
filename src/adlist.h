//
// Created by xyzjiao on 9/2/21.
//

#ifndef REDIS_ADLIST_H
#define REDIS_ADLIST_H

typedef struct listNode {
    struct listNode* prev;
    struct listNode* next;
    void* value;
} listNode;

typedef struct listIter {
    listNode* next;
    int direction
} listIter;

typedef struct list {
    listNode* head;
    listNode* tail;
    void* (*dup) (void* ptr);   // 复制node存放value的函数
    void* (*free) (void* ptr);  // 释放node存放value的函数
    int (*match) (void* ptr, void* key);    // 匹配node存放value的函数，返回0表示不相同，返回1表示相同
    unsigned long len;
} list;

#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

#define listSetDupMethod(l, m) ((l)->dup = (m))
#define listSetFreeMethod(l, m) ((l)->free = (m))
#define listSetMatchMethod(l, m) ((l)->match = (m))

#define listGetDupMethod(l) ((l)->dup)
#define listGetFreeMethod(l) ((l)->free)
#define listGetMatchMethod(l) ((l)->match)


list* listCreate(void);
void listRelease(list* list);
void listEmpty(list *list);
list* listAddNodeHead(list* list, void* value);
list* listAddNodeTail(list* list, void* value);
list* listInsertNode(list* list, listNode* old_node, void* value, int after);
void listDelNode(list* list, listNode* node);
listIter* listGetIterator(list* list, int direction);
void listReleaseIterator(listIter* iter);
void listRewind(list* list, listIter* iter);
void listRewindTail(list* list, listIter* iter);
listNode* listNext(listIter* iter);
list* listDup(list* org);
listNode* listSearchKey(list* list, void* key);
listNode* listIndex(list* list, long index);
void listRotate(list* list);
void listJoin(list* l, list* o);


// 迭代器的方向
#define AL_START_HEAD 0
#define AL_START_TAIL 1
#endif //REDIS_ADLIST_H
