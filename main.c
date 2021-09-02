#include <stdio.h>


void get_next(char * s, size_t len, int *next) {
    int i, j;
    i = 0;
    j = -1;

    next[0] = -1;

    while (i < len - 1) {
        // s[i]后缀字符，s[j]前缀字符
        if (j == -1 || s[j] == s[i]) {
            ++j;
            ++i;
            next[i] = j;
        }
        else {
            j = next[j];
        }

    }
}

int index_kpm(const char * s, size_t slen, const char * t, size_t tlen, size_t pos) {
    int i = pos;    // S下标

    int j = 0;      // T下标

    int next[tlen];

    get_next(t, tlen, next);

    for (int i = 0; i < sizeof(next) / sizeof(int); i++) {
        printf("%d, ", next[i]);
    }
    printf("\n");

    while (i < (int)slen && j < (int)tlen) {
        if (j == -1 || t[j] == s[i]){
            ++j;
            ++i;
        } else {
            j = next[j];
        }
    }

    printf("j: %d, i: %d\n", j, i);

    if (j > tlen - 1)
        return i - tlen;
    else
        return 0;

}

int main() {

    int index = index_kpm("abcabx", 6, "ca", 2, 0);
    printf("%d\n", index);




    return 0;
}
