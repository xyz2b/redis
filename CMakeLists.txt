cmake_minimum_required(VERSION 3.19)
project(redis C)

set(CMAKE_C_STANDARD 99)

add_executable(redis main.c src/sds.c src/sds.h src/sdsalloc.h)