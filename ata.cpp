#include "disk.h"

#define ATA_DATA     0x1F0
#define ATA_STATUS   0x1F7
#define ATA_COMMAND  0x1F7

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ata_wait()
{
    while (inb(ATA_STATUS) & 0x80); // BUSY
}

static int ata_read(disk_t* d, uint64_t lba, void* buf, uint32_t count)
{
    uint16_t* out = (uint16_t*)buf;

    for (uint32_t i = 0; i < count; i++)
    {
        ata_wait();

        outb(ATA_COMMAND, 0x20); // READ SECTORS

        uint16_t* sector = out + (i * 256);

        for (int j = 0; j < 256; j++)
        {
            sector[j] = inb(ATA_DATA);
            sector[j] |= (inb(ATA_DATA) << 8);
        }
    }

    return 0;
}

static int ata_write(disk_t*, uint64_t, const void*, uint32_t) { return 0; }
static int ata_flush(disk_t*) { return 0; }
