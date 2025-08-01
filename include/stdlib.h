#ifndef STDLIB_H
#define STDLIB_H

#include "stdint.h"

void* malloc(unsigned int size);
void free(void* ptr);
void exit(int status);

#endif /* STDLIB_H */
