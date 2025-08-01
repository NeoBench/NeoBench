#ifndef STRING_H
#define STRING_H

#include "stdint.h"

void* memset(void* dest, int val, unsigned int len);
void* memcpy(void* dest, const void* src, unsigned int len);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, unsigned int n);
int strcmp(const char* s1, const char* s2);
unsigned int strlen(const char* s);

#endif /* STRING_H */
