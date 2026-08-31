#include <stdint.h>
#include "../../shared/libc/memory.h"

#include "cache.h"
#include "../fs/disk.h"

typedef struct
{
    uint32_t block;

    uint8_t data[NBFS_BLOCK_SIZE];

    int valid;

} cache_entry_t;

static cache_entry_t cache[CACHE_BLOCKS];

int cache_init(void)
{
    memset(cache,0,sizeof(cache));

    return 1;
}

int cache_read(
    uint32_t block,
    void *buffer)
{
    for(int i=0;i<CACHE_BLOCKS;i++)
    {
        if(cache[i].valid &&
           cache[i].block==block)
        {
            memcpy(
                buffer,
                cache[i].data,
                NBFS_BLOCK_SIZE);

            return 1;
        }
    }

    if(!disk_read_blocks(
            block,
            1,
            buffer))
        return 0;

    return 1;
}
