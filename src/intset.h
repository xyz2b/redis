//
// Created by xyzjiao on 9/17/21.
//

#ifndef REDIS_INTSET_H
#define REDIS_INTSET_H
#include <inttypes.h>

typedef struct intset {
    uint32_t  encoding;
    uint32_t  length;
    int8_t  contents[];
} intset;

intset *intsetNew(void);
intset *intsetAdd(intset *is, int64_t value, uint8_t *success);
intset *intsetRemove(intset *is, int64_t value, int *success);
uint8_t intsetFind(intset *is, int64_t value);
int64_t intsetRandom(intset *is);
uint8_t intsetGet(intset *is, uint32_t pos, int64_t *value);
uint32_t intsetLen(const intset *is);
size_t intsetBlobLen(intset *is);


#endif //REDIS_INTSET_H
