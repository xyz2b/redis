//
// Created by xyzjiao on 9/22/21.
//

#include "server.h"
#include "atomicvar.h"

// 以分钟为单位返回当前时间戳，只保留后16位
unsigned long LFUGetTimeInMinutes(void) {
    return (server.unixtime / 60) & 0xFFFF;
}

// 计算全局 LRU 时钟值
/**
 * 首先，getLRUClock 函数将获得的 UNIX 时间戳，除以 LRU_CLOCK_RESOLUTION 后，就得到了以 LRU 时钟精度来计算的 UNIX 时间戳，也就是当前的 LRU 时钟值。
 * 紧接着，getLRUClock 函数会把 LRU 时钟值和宏定义 LRU_CLOCK_MAX 做与运算，其中宏定义 LRU_CLOCK_MAX 表示的是 LRU 时钟能表示的最大值。
 * */
unsigned int getLRUClock(void) {
    return (mstime()/LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

// 返回当前的全局 LRU 时钟值
unsigned int LRU_CLOCK(void) {
    unsigned int lruclock;
    if (1000 / server.hz <= LRU_CLOCK_RESOLUTION) {
        atomicGet(server.lruclock, lruclock);
    } else {
        lruclock = getLRUClock();
    }
    return lruclock;
}