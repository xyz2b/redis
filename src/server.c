//
// Created by xyzjiao on 9/22/21.
//

#include <sys/time.h>
#include <stdlib.h>
#include "server.h"

// 全局共享变量
struct sharedObjectsStruct shared;
struct redisServer server;

// 获取以微秒为单位的当前UNIX时间戳
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_usec;
    return ust;
}

// 获取以毫秒为单位的当前UNIX时间戳
mstime_t mstime(void) {
    return ustime() / 1000;
}