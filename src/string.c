#include "../include/string.h"

void* memset(void* dest, int val, unsigned int len) {
    unsigned char* ptr = (unsigned char*)dest;
    while (len-- > 0)
        *ptr++ = val;
    return dest;
}

void* memcpy(void* dest, const void* src, unsigned int len) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (len--)
        *d++ = *s++;
    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++) != 0);
    return dest;
}

char* strncpy(char* dest, const char* src, unsigned int n) {
    char* d = dest;
    while (n > 0 && (*d++ = *src++) != 0)
        n--;
    while (n-- > 0)
        *d++ = 0;
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

unsigned int strlen(const char* s) {
    const char* p = s;
    while (*p)
        p++;
    return (unsigned int)(p - s);
}
