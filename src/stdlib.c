#include "../include/stdlib.h"

// This is a very simple memory allocator for demonstration purposes
#define HEAP_SIZE 65536
static unsigned char heap[HEAP_SIZE];
static unsigned int heap_next = 0;

void* malloc(unsigned int size) {
    // Align to 4 bytes
    size = (size + 3) & ~3;
    
    if (heap_next + size > HEAP_SIZE)
        return (void*)0; // Out of memory
        
    void* ptr = &heap[heap_next];
    heap_next += size;
    return ptr;
}

void free(void* ptr) {
    // This is a very simple allocator, we don't free memory
    // Just a stub for compatibility
    (void)ptr;
}

void exit(int status) {
    // Cannot exit in ROM, this is just a stub
    while(1);
}
