#include <stdio.h>
#include "src/sds.h"

int main() {
    char *s = "test";
    sds c = sdsnewlen(s, 4);

    printf("%s", c);

    return 0;
}
