#include <stdio.h>
#include <string.h>
#include "src/sds.h"
#include "src/adlist.h"

void get_next(char * s, size_t len, int *next) {
    int i, j;
    i = 0;
    j = -1;

    next[0] = -1;

    while (i < len - 1) {
        // s[i]后缀字符，s[j]前缀字符
        if (j == -1 || s[j] == s[i]) {
            ++j;
            ++i;
            next[i] = j;
        }
        else {
            j = next[j];
        }

    }
}

int index_kpm(const char * s, size_t slen, const char * t, size_t tlen, size_t pos) {
    int i = pos;    // S下标

    int j = 0;      // T下标

    int next[tlen];

    get_next(t, tlen, next);

    for (int i = 0; i < sizeof(next) / sizeof(int); i++) {
        printf("%d, ", next[i]);
    }
    printf("\n");

    while (i < (int)slen && j < (int)tlen) {
        if (j == -1 || t[j] == s[i]){
            ++j;
            ++i;
        } else {
            j = next[j];
        }
    }

    printf("j: %d, i: %d\n", j, i);

    if (j > tlen - 1)
        return i - tlen;
    else
        return 0;

}

int main() {
//    list *list, *o;
//    listNode* n;
//
//    list = listCreate();
//    o = listCreate();
//
//
//    int i = 0;
//    int j = 1;
//    listAddNodeTail(list, &i);
//    listAddNodeTail(list, &j);
//
//    listAddNodeTail(o, &i);
//    listAddNodeTail(o, &j);
//
//    listJoin(list, o);
//
//    listIter* iter;
//    iter = listGetIterator(list, AL_START_HEAD);
//    while ((n = listNext(iter)) != NULL) {
//        printf("%d\n", *(int*)(n->value));
//    }

    return 0;
}
