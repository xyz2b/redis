//
// Created by xyzjiao on 9/6/21.
//

#ifndef REDIS_ENDIARCONV_H
#define REDIS_ENDIARCONV_H
#include "config.h"

#if (BYTE_ORDER == LITTLE_ENDIAN)
#define memrev16ifbe(p) ((void)(0))
#define memrev32ifbe(p) ((void)(0))
#define memrev64ifbe(p) ((void)(0))

#define intrev16ifbe(v) (v)
#define intrev32ifbe(v) (v)
#define intre64ifbe(v) (v)

#else

#endif

#endif //REDIS_ENDIARCONV_H
