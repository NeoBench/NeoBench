/*
 * bootblock.c
 * NeoBench mkfs.nbfs
 */

#include <stdint.h>
#include <string.h>

#include "layout.h"
#include "fs/bootblock.h"

int nbfs_write_bootblock(FILE *fp)
{
    uint8_t block[4096];

    memset(block, 0, sizeof(block));

    memcpy(block, "NBBOOT", 6);

    if (fseek(fp, 0, SEEK_SET) != 0)
        return -1;

    if (fwrite(block, sizeof(block), 1, fp) != 1)
        return -1;

    fflush(fp);

    return 0;
}
