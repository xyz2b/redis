//
// Created by xyzjiao on 9/17/21.
//

#include <string.h>
#include <inttypes.h>
#include "intset.h"
#include "endiarconv.h"
#include "zmalloc.h"

#define INTSET_ENC_INT16 (sizeof(int16_t))
#define INTSET_ENC_INT32 (sizeof(int32_t))
#define INTSET_ENC_INT64 (sizeof(int64_t))

static uint8_t _intsetValueEncoding(int64_t v) {
    if (v < INT32_MIN || v > INT32_MAX)
        return INTSET_ENC_INT64;
    else if (v < INT16_MIN || v > INT16_MAX)
        return INTSET_ENC_INT32;
    else
        return INTSET_ENC_INT16;
}

// 根据给定的编码类型，返回pos位置的值
static int64_t _intsetGetEncoded(intset* is, int pos, uint8_t enc) {
    int64_t v64;
    int32_t v32;
    int16_t v16;

    if (enc == INTSET_ENC_INT64) {
        memcpy(&v64, ((int64_t*)is->contents) + pos, sizeof(v64));
        memrev64ifbe(&v64);
        return v64;
    } else if (enc == INTSET_ENC_INT32) {
        memcpy(&v32, ((int32_t*)is->contents) + pos, sizeof(v32));
        memrev32ifbe(&v32);
        return v32;
    } else {
        memcpy(&v16, ((int16_t*)is->contents) + pos, sizeof(v16));
        memrev16ifbe(&v16);
        return v16;
    }
}

// 返回pos位置的元素值
static int64_t _intsetGet(intset* is, int pos) {
    return _intsetGetEncoded(is, pos, intrev32ifbe(is->encoding));
}

// 设置pos位置的元素值
static void _intsetSet(intset* is, int pos, int64_t value) {
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        ((int64_t*)is->contents)[pos] = value;
        memrev64ifbe(((int64_t*)is->contents)+pos);
    } else if (encoding == INTSET_ENC_INT32) {
        ((int32_t*)is->contents)[pos] = value;
        memrev32ifbe(((int32_t*)is->contents)+pos);
    } else {
        ((int16_t*)is->contents)[pos] = value;
        memrev16ifbe(((int16_t*)is->contents)+pos);
    }
}

intset* intsetNew(void) {
    intset* is = zmalloc(sizeof(intset));
    is->encoding = intrev32ifbe(INTSET_ENC_INT16);
    is->length = 0;
    return is;
}

// 调整大小为多少个元素，len表示intset中有多少个元素
static intset* intsetResize(intset* is, uint32_t len) {
    uint32_t size = len * intrev32ifbe(is->encoding);
    is = zrealloc(is, sizeof(intset) + size);
    return is;
}

// intset是有序集合
// 查找value
// 返回1，表示查到了，pos返回value的位置
// 返回0，表示没有查到
// pos返回大于等于value的最小值的位置，即如果是找到了value就返回value的位置，如果没有找到value就返回大于value的最小值的位置
static uint8_t intsetSearch(intset* is, int64_t value, uint32_t* pos) {
    int min = 0, max = intrev32ifbe(is->length) - 1, mid = -1;
    int64_t cur = -1;

    if (intrev32ifbe(is->length) == 0) {
        if (pos) *pos = 0;
        return 0;
    } else {
        if (value > _intsetGet(is, max)) {  // 要查找的值比intset中最大值还要大
            if (pos) *pos = intrev32ifbe(is->length);
            return 0;
        } else if (value < _intsetGet(is, 0)) { // 要查找的值比intset中最小值还要小
            if (pos) *pos = 0;
            return 0;
        }
    }

    // 二分查找法
    while (max >= min) {    // 循环退出条件之一 min > max
        mid = ((unsigned int)min + (unsigned int)max) >> 1; // 右移1，相当于/2
        cur = _intsetGet(is, mid);
        if (value > cur) {
            min = mid + 1;
        } else if (value < cur) {
            max = mid - 1;
        } else {    // value == cur，表示找到了，也会退出循环
            break;
        }
    }

    if (value == cur) {
        if (pos) *pos = mid;
        return 1;
    } else {
        if (pos) *pos = min;
        return 0;
    }
}

// 涉及到需要升级编码，即代表插入的元素的值要么大于现有的所有值（正数），要么小于现有的所有值（负数）
// 即要插入的元素要么是在intset首部插入，要么是以在头部插入(负值开头，正值结尾)
// 返回intset，因为涉及到intset内存重分配，地址可能发生变化
static intset* intsetUpgradeAndAdd(intset* is, int64_t value) {
    uint8_t  curenc = intrev32ifbe(is->encoding);
    uint8_t newenc = _intsetValueEncoding(value);
    int length = intrev32ifbe(is->length);
    int prepend = value < 0 ? 1 : 0;    // 负值在开头插入，正值在末尾插入

    is->encoding = intrev32ifbe(newenc);
    is = intsetResize(is, intrev32ifbe(is->length)+1);

    while (length--)
        // 这里调整原本元素位置的做法是 将对应位置的元素，再重新插入到扩容后的intset中相应的位置
        // 如果新元素是在开头插入（负值），那就将原本位置(元素索引)的元素，再重新插入到原本位置后一个位置，然后在开头预留一个空间出来插入新元素
        // 如果是在末尾插入（正值），那就将原本位置的元素，再重新插入到扩容后的intset中原先的位置处即可
        _intsetSet(is, length+prepend, _intsetGetEncoded(is, length, curenc));

    if (prepend)    // 在开头插入新元素
        _intsetSet(is, 0, value);
    else    // 在末尾插入新元素
        _intsetSet(is, intrev32ifbe(is->length), value);

    is->length = intrev32ifbe(intrev32ifbe(is->length) + 1);
    return is;
}

// 将from位置开始到结尾的元素，移动到从to开始的位置
static void intsetMoveTail(intset* is, uint32_t from, uint32_t to) {
    void* src, *dst;
    uint32_t bytes = intrev32ifbe(is->length) - from;   // 从from到结尾的元素个数
    uint32_t encoding = intrev32ifbe(is->encoding);

    if (encoding == INTSET_ENC_INT64) {
        src = (int64_t*)is->contents + from;
        dst = (int64_t*)is->contents + to;
        bytes *= sizeof(int64_t);
    } else if (encoding == INTSET_ENC_INT32) {
        src = (int32_t*)is->contents + from;
        dst = (int32_t*)is->contents + to;
        bytes *= sizeof(int32_t);
    } else {
        src = (int16_t*)is->contents + from;
        dst = (int16_t*)is->contents + to;
        bytes *= sizeof(int16_t);
    }
    memmove(dst, src, bytes);
}

// 在intset中添加一个元素
// success为1表示插入成功，0表示插入失败
// 返回插入元素后的intset，因为涉及内存重新分配
intset* intsetAdd(intset* is, int64_t value, uint8_t* success) {
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t  pos;
    if (success) *success = 1;

    if (valenc > intrev32ifbe(is->encoding)) {  // 需要升级编码
        return intsetUpgradeAndAdd(is, value);
    } else {    // 不用升级编码
        if (intsetSearch(is, value, &pos)) {  // 要插入元素已经存在于set中了，不会重复插入
            if (success) *success = 0;
            return is;
        }

        // 如果插入元素不存在，pos返回大于value的最小值的位置，即需要插入的位置，将新元素插入到这个值之前
        is = intsetResize(is, intrev32ifbe(is->length) + 1);
        if (pos < intrev32ifbe(is->length)) intsetMoveTail(is, pos, pos + 1);   // 预留插入元素的空间
    }

    _intsetSet(is, pos, value);
    is->length = intrev32ifbe(intrev32ifbe(is->length) + 1);
    return is;
}

// 返回值是删除完之后的intset，因为涉及内存重分配，success为1是删除成功
intset* intsetRemove(intset* is, int64_t value, int* sucess) {
    uint8_t valenc = _intsetValueEncoding(value);
    uint32_t pos;
    if (sucess) *sucess = 0;

    if (valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, &pos)) {    // 找到了要删除的元素
        uint32_t len = intrev32ifbe(is->length);

        if (sucess) *sucess = 1;

        // 删除元素
        if (pos < (len - 1)) intsetMoveTail(is, pos + 1, pos);
        is = intsetResize(is, len - 1);
        is->length = intrev32ifbe(len - 1);
    }
    return is;
}

// 查找某个元素是否在intset中
uint8_t intsetFind(intset* is, int64_t value) {
    uint8_t valenc = _intsetValueEncoding(value);
    return valenc <= intrev32ifbe(is->encoding) && intsetSearch(is, value, NULL);
}

// 随机返回一个intset中的元素
int64_t intsetRandom(intset* is) {
    return _intsetGet(is, rand() % intrev32ifbe(is->length));
}

// 获取某个位置(索引)的元素值
// value返回元素值，函数返回获取成功失败
uint8_t intsetGet(intset* is, uint32_t pos, int64_t* value) {
    if (pos < intrev32ifbe(is->length)) {
        *value = _intsetGet(is, pos);
        return 1;
    }
    return 0;
}

// 获取intset元素个数
uint32_t intsetLen(const intset* is) {
    return intrev32ifbe(is->length);
}

// 返回intset的总长度
size_t intsetBlobLen(intset* is) {
    return sizeof(intset) + intrev32ifbe(is->length) * intrev32ifbe(is->encoding);
}