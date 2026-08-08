#include "string.h"

size_t strlen(const char *s)
{
    size_t n = 0;

    while (*s++)
        n++;

    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;

    while ((*d++ = *src++))
        ;

    return dest;
}
