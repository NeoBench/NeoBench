#ifndef MKFS_H
#define MKFS_H

#include <stdint.h>

int mkfs_create(const char *image);
int mkfs_create_ex(const char *image, uint64_t size_bytes);

/*
 * Image size in use by the current mkfs run (bytes).
 */
uint64_t mkfs_image_size(void);

int nbfs_create_root_inode(FILE *fp);

#endif
