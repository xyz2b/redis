//
// Created by xyzjiao on 9/15/21.
//

#ifndef REDIS_LISTPACK_MALLOC_H
#define REDIS_LISTPACK_MALLOC_H
#include "zmalloc.h"
#define lp_malloc zmalloc
#define lp_realloc zrealloc
#define lp_free zfree
#endif //REDIS_LISTPACK_MALLOC_H
