#ifndef NEO_BENCH_STRING_H
#define NEO_BENCH_STRING_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
int strcmp(const char *a, const char *b);

#endif
