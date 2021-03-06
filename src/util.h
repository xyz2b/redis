//
// Created by xyzjiao on 9/7/21.
//

#ifndef REDIS_UTIL_H
#define REDIS_UTIL_H

#include <stddef.h>

int string2ll(const char* s, size_t slen, long long* value);
int ll2string(char *dst, size_t dstlen, long long svalue);
int string2l(const char *s, size_t slen, long *lval);
int stringmatch(const char *pattern, const char *string, int nocase);
int stringmatchlen(const char *pattern, int patternLen,
                   const char *string, int stringLen, int nocase);

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif //REDIS_UTIL_H
