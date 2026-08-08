#include "image.h"

#include <stdio.h>
#include <stdint.h>

#define IMAGE_SIZE (64 * 1024 * 1024)

int image_create(const char *filename)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return -1;

    uint8_t zero[4096] = {0};

    for (size_t i = 0; i < IMAGE_SIZE / sizeof(zero); i++)
        fwrite(zero, sizeof(zero), 1, fp);

    fclose(fp);

    return 0;
}
