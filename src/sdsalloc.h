//
// Created by xyzjiao on 8/31/21.
//

#ifndef REDIS_SDSALLOC_H
#define REDIS_SDSALLOC_H
#include <stdlib.h>

// 方便后续更换内存分配器
#define s_malloc malloc
#define s_free free

#endif //REDIS_SDSALLOC_H
