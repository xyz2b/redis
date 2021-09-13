//
// Created by xyzjiao on 9/7/21.
//

#ifndef REDIS_UTIL_H
#define REDIS_UTIL_H

#include <stddef.h>

int string2ll(const char* s, size_t slen, long long* value);


#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif //REDIS_UTIL_H
