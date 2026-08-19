#include <stddef.h>

void *memset(void *dest, int value, size_t count)
{
    unsigned char *p = (unsigned char *)dest;
    unsigned char v = (unsigned char)value;

    while (count--)
        *p++ = v;

    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    while (count--)
        *d++ = *s++;

    return dest;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}
