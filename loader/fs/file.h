#ifndef NB_FILE_H
#define NB_FILE_H

#include <stdint.h>
#include <nbfs/inode.h>

typedef struct
{
    uint32_t first_block;
    uint32_t size;
    uint32_t position;
} nb_file_t;

int nb_open(const char *path, nb_file_t *file);
int nb_read(nb_file_t *file, void *buffer, uint32_t bytes);
int nb_read_inode_file(const nbfs_inode_t *inode, void *buffer);
int nb_close(nb_file_t *file);

#endif
