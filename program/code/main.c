#include "getZipFile.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    FILE* fin = fopen("test1.zip", "rb");
    unsigned char* in = (unsigned char*)malloc(150000000);
    int size = 0;
    void* zm = NULL;

    printf("开始载入");
    while (!feof(fin)) {
        in[size++] = getc(fin);
    }
    printf("载入完毕, %d\n", size);

    getZipInterface(in, size, &zm);
    

    free(in);
    return 0;
}