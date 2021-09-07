//
// Created by xyzjiao on 9/2/21.
//

#include "adlist.h"
#include "zmalloc.h"

// 创建list
list* listCreate(void) {
    struct list* list;

    if ((list = zmalloc(sizeof(*list))) == NULL) return NULL;

    list->head = list->tail = NULL;
    list->len = 0;
    list->dup = NULL;
    list->match = NULL;
    list->free = NULL;

    return list;
}

// 清空list
void listEmpty(list *list) {
    unsigned long len;
    listNode *current, *next;

    current = list->head;
    len = list->len;
    while (len--) {
        next = current->next;
        if (list->free) list->free(current->value);
        zfree(current);
        current = next;
    }
    list->head = list->tail = NULL;
    list->len = 0;
}

// 释放list
void listRelease(list* list) {
    listEmpty(list);
    zfree(list);
}

// 在链表头添加节点
list* listAddNodeHead(list* list, void* value) {
    listNode* node;

    if ((node = zmalloc(sizeof(*node))) == NULL) return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
    return list;
}

// 在链表尾添加节点
list* listAddNodeTail(list* list, void* value) {
    listNode* node;

    if ((node = zmalloc(sizeof(*node))) == NULL) return NULL;
    node->value = value;
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->next = NULL;
        node->prev = list->tail;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
    return list;
}

// 在链表中某个节点之前或之后插入节点
list* listInsertNode(list* list, listNode* old_node, void* value, int after) {
    listNode* node;

    if ((node = zmalloc(sizeof(*node))) == NULL) return NULL;
    node->value = value;
    // 在之后插入
    if (after) {
        node->prev = old_node;
        node->next = old_node->next;
        // 如果old_node是链表尾，需要调整链表的尾指针
        if (list->tail == old_node)
            list->tail = node;
    } else {    // 在之前插入
        node->next = old_node;
        node->prev = old_node->prev;
        // 如果old_node是链表头，需要调整链表的头指针
        if (list->head == old_node)
            list->head = node;
    }

    // 在之后插入，如果old_node->next不为空，需要调整old_node->next->prev指针，指向插入的节点
    // 插入node后即需要调整node->next->prev指针，指向插入的节点
    if (node->next != NULL)
        node->next->prev = node;

    // 在之前插入，如果old_node->prev不为空，需要调整old_node->prev->next指针，指向插入的节点
    // 插入node后即需要调整node->prev->next指针，指向插入的节点
    if (node->prev != NULL)
        node->prev->next = node;

    list->len++;
    return list;
}

// 删除节点
void listDelNode(list* list, listNode* node) {
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    if (list->free) list->free(node->value);
    zfree(node);
    list->len--;
}

// 返回list的迭代器，每次调用listNext()，就会根据方向返回list中下一个元素
listIter* listGetIterator(list* list, int direction) {
    listIter* iter;

    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;
    if (direction == AL_START_HEAD)
        iter->next = list->head;
    else
        iter->next = list->tail;
    iter->direction = direction;
    return iter;
}

// 释放list迭代器
void listReleaseIterator(listIter* iter) {
    zfree(iter);
}

// 在现有的listIter结构体中创建list迭代器，方向从头到尾
void listRewind(list* list, listIter* iter) {
    iter->next = list->head;
    iter->direction = AL_START_HEAD;
}

// 在现有的listIter结构体中创建list迭代器，方向从尾到头
void listRewindTail(list* list, listIter* iter) {
    iter->next = list->tail;
    iter->direction = AL_START_TAIL;
}

// 从迭代器中获取下一个元素
listNode* listNext(listIter* iter) {
    listNode* current = iter->next;

    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next;
        else
            iter->next = current->prev;
    }

    return current;
}


// copy list
list* listDup(list* org) {
    list* copy;
    listIter iter;
    listNode* node;

    if ((copy = listCreate()) == NULL) return NULL;
    copy->dup = org->dup;
    copy->free = org->free;
    copy->match = org->match;

    listRewind(org, &iter);
    while ((node = listNext(&iter)) != NULL) {
        void* value;

        if (copy->dup) {    // 如果有value的复制函数，使用复制函数复制新的一份value出来，对应深copy
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                return NULL;
            }
        } else {    // 如果没有value的复制函数，就直接让新的list中的node value 指向 原来list的node value所指向的内容，对应浅copy
            value = node->value;
        }

        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            return NULL;
        }
    }

    return copy;
}

// 搜索包含key的list节点
listNode* listSearchKey(list* list, void* key) {
    listIter iter;
    listNode* node;

    listRewind(list, &iter);
    while ((node = listNext(&iter)) != NULL) {
        if (list->match) {
            if (list->match(node->value, key))
                return node;
        } else {
            if (node->value == key)
                return node;
        }
    }
    return NULL;
}

// 返回对应索引位置的listNode，index为负值表示从尾到头遍历，index尾正数表示从头到尾遍历
listNode* listIndex(list* list, long index) {
    listNode* n;

    if (index < 0) {
        index = (-index) - 1;
        n = list->tail;
        // 从尾向头遍历
        while (index-- && n) n = n->prev;
    } else {
        n = list->head;
        // 从头向尾遍历
        while (index-- && n) n = n->next;
    }
    return n;
}

// 将list的tail节点移动到list的head处
void listRotate(list* list) {
    listNode* tail = list->tail;

    if (listLength(list) <= 1) return;

    list->tail = tail->prev;
    list->tail->next = NULL;

    list->head->prev = tail;
    tail->next = list->head;
    tail->prev = NULL;
    list->head = tail;
}

// 将list o中的所有node添加到list l末尾，o变成空链表
void listJoin(list* l, list* o) {
    // 如果o为空，直接返回即可
    if (o->len == 0) return;

    // 如果l为空，直接将l的头尾指针指向o的头尾即可
    if (l->len == 0) {
        l->head = o->head;
        l->tail = o->tail;
    } else {  // 如果l不为空，o也不为空
        // 处理o指向l的指针
        o->head->prev = l->tail;
        // 处理l指向o的指针
        l->tail->next = o->head;
        // 处理l的tail指针
        l->tail = o->tail;
    }

    l->len += o->len;

    o->head = o->tail = NULL;
    o->len = 0;
}