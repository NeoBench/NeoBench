#include <stdint.h>
#include "disk.h"

int disk_init(void)
{
    return 1;
}

int disk_read_blocks(
    uint32_t block,
    uint32_t count,
    void *buffer)
{
    (void)block;
    (void)count;
    (void)buffer;

    /*
     * Next step:
     * Replace with ATA/SCSI/virtual disk reads.
     */

    return 1;
}
