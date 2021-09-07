//
// Created by xyzjiao on 9/7/21.
//

#include <stdio.h>
#include <stdlib.h>

void _serverAssert(char *estr, char *file, int line) {
    printf("[%s] (%s:%d): %s\n", "ERROR", file, line, estr);
    exit(-1);
}

void _serverPanic(const char *file, int line, const char *msg, ...) {
    printf("[%s] (%s:%d): %s\n", "ERROR", file, line, msg);
    exit(-1);
}