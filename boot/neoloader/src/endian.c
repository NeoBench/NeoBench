#include <stdint.h>

uint16_t neo_be16(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;

    return ((uint16_t)p[0] << 8) |
           (uint16_t)p[1];
}

uint32_t neo_be32(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;

    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}
