#ifndef NBFS_FS_DIRECTORY_H
#define NBFS_FS_DIRECTORY_H

#include <stdio.h>
#include <stdint.h>

#include <nbfs/nbfs.h>

/*
 * Write the root directory to the supplied data block.
 */
int nbfs_write_root_directory(FILE *fp, uint64_t block);

#endif /* NBFS_FS_DIRECTORY_H */
