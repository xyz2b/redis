//
// Created by xyzjiao on 9/2/21.
//

#ifndef REDIS_ZMALLOC_H
#define REDIS_ZMALLOC_H
#include <stdlib.h>

#define zmalloc malloc
#define zfree free
#define zcalloc calloc
#define zrealloc realloc

#endif //REDIS_ZMALLOC_H
