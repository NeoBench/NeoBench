#include <stdio.h>
#include <stdint.h>

FILE *image_create(const char *path,uint64_t bytes)
{
    FILE *fp=fopen(path,"wb+");

    if(!fp)
        return NULL;

    fseek(fp,bytes-1,SEEK_SET);
    fputc(0,fp);
    rewind(fp);

    return fp;
}
