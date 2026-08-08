#ifndef NB_MEMORY_H
#define NB_MEMORY_H

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);

void *memset(void *dest, int value, size_t n);

int memcmp(const void *a, const void *b, size_t n);

#endif
