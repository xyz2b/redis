//
// Created by xyzjiao on 9/22/21.
//

#ifndef REDIS_ATOMICVAR_H
#define REDIS_ATOMICVAR_H
#include <pthread.h>

#define atomicGet(var, dstvar) do { \
    pthread_mutex_lock(&var ## _mutex); \
    dstvar = var;                   \
    pthread_mutex_unlock(&var ## _mutex);\
} while(0)


#endif //REDIS_ATOMICVAR_H
