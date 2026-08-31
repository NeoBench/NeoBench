#include <stddef.h>

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++)
        n++;
    return n;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b)
    {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

void *memset(void *dest, int value, size_t count)
{
    unsigned char *p = (unsigned char *)dest;
    unsigned char v = (unsigned char)value;

    while (count--)
        *p++ = v;

    return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;

    for (size_t i = 0; i < count; i++)
    {
        if (pa[i] != pb[i])
            return pa[i] - pb[i];
    }

    return 0;
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
