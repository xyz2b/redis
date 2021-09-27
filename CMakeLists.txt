cmake_minimum_required(VERSION 3.19)
project(redis C)

set(CMAKE_C_STANDARD 99)

add_executable(redis main.c src/sds.c src/sds.h src/sdsalloc.h src/dict.c src/dict.h src/adlist.c src/adlist.h src/zmalloc.c src/zmalloc.h src/siphash.c src/ziplist.c src/ziplist.h src/endiarconv.h src/config.h src/redisassert.h src/debug.c src/util.c src/util.h src/server.h src/t_zset.c src/quicklist.c src/quicklist.h src/listpack.c src/listpack.h src/listpack_malloc.h src/intset.c src/intset.h src/t_string.c src/networking.c src/server.c src/object.c src/evict.c src/atomicvar.h src/db.c src/blocked.c src/lazyfree.c src/notify.c src/t_list.c src/multi.c src/t_hash.c src/t_set.c src/t_set.h)