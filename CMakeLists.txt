cmake_minimum_required(VERSION 3.19)
project(redis C)

set(CMAKE_C_STANDARD 99)

add_executable(redis main.c src/sds.c src/sds.h src/sdsalloc.h src/dict.c src/dict.h src/adlist.c src/adlist.h src/zmalloc.c src/zmalloc.h src/siphash.c src/ziplist.c src/ziplist.h src/endiarconv.h src/config.h src/redisassert.h src/debug.c src/util.c src/util.h src/server.h src/t_zset.c)