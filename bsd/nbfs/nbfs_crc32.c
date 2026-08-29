/*
 * NeoBench File System (NBFS) -- CRC32 checksum.
 *
 * Standard CRC32 (Ethernet/PKZIP) used for on-disk metadata verification.
 * Polynomial: 0xEDB88320 (reflected).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include "nbfs.h"

static uint32_t nbfs_crc32_table[256];
static int nbfs_crc32_table_ready;

static void
nbfs_crc32_init(void)
{
	uint32_t poly = 0xEDB88320;
	uint32_t crc;
	int i, j;

	for (i = 0; i < 256; i++) {
		crc = (uint32_t)i;
		for (j = 0; j < 8; j++) {
			if (crc & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc >>= 1;
		}
		nbfs_crc32_table[i] = crc;
	}

	nbfs_crc32_table_ready = 1;
}

uint32_t
nbfs_crc32(const void *buf, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	uint32_t crc;

	if (!nbfs_crc32_table_ready)
		nbfs_crc32_init();

	crc = 0xFFFFFFFF;

	while (len--)
		crc = nbfs_crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return (crc ^ 0xFFFFFFFF);
}
