#include <stdint.h>

typedef struct {
    uint32_t start;
    uint32_t size;
    uint32_t type;
} mem_region_t;

mem_region_t memmap[] = {
    {0x00000000, 0x0009FFFF, 1},
    {0x00100000, 0x00FFFFFF, 1}, // kernel
    {0x01000000, 0x01FFFFFF, 2}, // reserved modules
};
