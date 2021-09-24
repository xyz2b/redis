//
// Created by xyzjiao on 9/22/21.
//

#include "server.h"

void addReply(client* c, robj* obj) {

}

void addReplyError(client* c, const char* err) {

}

void addReplyErrorFormat(client* c, const char* fmt, ...) {

}

void addReplyLongLong(client* c, long long ll) {

}

// 批量回复时，如果有多个数据需要一起回复，需要调用该函数设置下有多少个数据
void addReplyMultiBulkLen(client* c, long length) {

}

// add a redis object as a bulk(批量) reply
void addReplyBulk(client* c, robj* obj) {

}

// add a c buffer as bulk reply
void addReplyBulkBuffer(client* c, const void* p, size_t len) {

}