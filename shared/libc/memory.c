#include "memory.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;

    return dest;
}

void *memset(void *dest, int value, size_t n)
{
    unsigned char *d = dest;

    while (n--)
        *d++ = (unsigned char)value;

    return dest;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a;
    const unsigned char *y = b;

    while (n--)
    {
        if (*x != *y)
            return *x - *y;

        x++;
        y++;
    }

    return 0;
}
