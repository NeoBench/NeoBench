/*
 * nbfs-info
 *
 * NeoBench NBFS filesystem inspector
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <nbfs/nbfs.h>


typedef struct
{
    uint64_t inode;
    uint16_t record_length;
    uint8_t  name_length;
    uint8_t  type;
    char     name[252];

} nbfs_dirent_t;



static void dump_root_directory(FILE *fp, uint64_t block)
{
    uint8_t data[NBFS_DEFAULT_BLOCK_SIZE];

    if (fseek(fp,
              (long)(block * NBFS_DEFAULT_BLOCK_SIZE),
              SEEK_SET) != 0)
    {
        printf("Failed to seek root directory.\n");
        return;
    }


    if (fread(data, 1, sizeof(data), fp) != sizeof(data))
    {
        printf("Failed to read root directory.\n");
        return;
    }


    printf("\nRoot directory\n");
printf("--------------\n");

size_t offset = 0;

while (offset + 12 <= NBFS_DEFAULT_BLOCK_SIZE)
{
    nbfs_dirent_t *entry =
        (nbfs_dirent_t *)(data + offset);

    if (entry->record_length == 0)
        break;

    if (entry->record_length < 12 ||
        offset + entry->record_length >
        NBFS_DEFAULT_BLOCK_SIZE)
    {
        printf("Invalid directory record at offset %zu\n",
               offset);
        break;
    }

    if (entry->inode != 0)
    {
        printf("%.*s\n",
               entry->name_length,
               entry->name);

        printf("  inode: %llu\n",
               (unsigned long long)
               entry->inode);

        printf("  type:  %u\n",
               entry->type);
    }

    offset += entry->record_length;
}
}



int main(int argc, char **argv)
{
    FILE *fp;

    nbfs_superblock_t sb;
    nbfs_inode_t root;


    if (argc != 2)
    {
        printf("Usage: %s <image>\n",
               argv[0]);
        return 1;
    }


    fp = fopen(argv[1], "rb");

    if (!fp)
    {
        perror("fopen");
        return 1;
    }



    /*
     * Read superblock
     *
     * Block 1
     */
    if (fseek(fp,
              (long)(NBFS_SUPERBLOCK *
              NBFS_DEFAULT_BLOCK_SIZE),
              SEEK_SET) != 0)
    {
        fclose(fp);
        return 1;
    }


    if (fread(&sb,
              sizeof(sb),
              1,
              fp) != 1)
    {
        fclose(fp);
        return 1;
    }



    printf("Magic:        NBFS\n");

    printf("Version:      %u.%u\n",
           sb.version_major,
           sb.version_minor);

    printf("Block size:   %u\n",
           sb.block_size);

    printf("Blocks:       %llu\n",
           (unsigned long long)
           sb.total_blocks);

    printf("Free blocks:  %llu\n",
           (unsigned long long)
           sb.free_blocks);

    printf("Root inode:   %llu\n",
           (unsigned long long)
           sb.root_inode);

    printf("Volume:       %s\n",
           sb.volume_name);


    printf("\nFilesystem:   OK\n");



    /*
     * NBFS v1 inode table
     *
     * Blocks 4-67
     */
    uint64_t inode_offset =
        ((uint64_t)NBFS_INODE_TABLE *
         sb.block_size) +
        ((sb.root_inode - 1) *
         sizeof(nbfs_inode_t));


    printf("\nDEBUG inode offset: %llu\n",
           (unsigned long long)
           inode_offset);



    if (fseek(fp,
              (long)inode_offset,
              SEEK_SET) != 0)
    {
        printf("Failed inode seek.\n");
        fclose(fp);
        return 1;
    }



    if (fread(&root,
              sizeof(nbfs_inode_t),
              1,
              fp) != 1)
    {
        printf("Failed inode read.\n");
        fclose(fp);
        return 1;
    }



    printf("\n## Root inode\n\n");

    printf("inode: %llu\n",
           (unsigned long long)
           root.inode_number);

    printf("size:  %llu\n",
           (unsigned long long)
           root.size);

    printf("links: %u\n",
           root.links);


    printf("extent[0] start: %llu\n",
           (unsigned long long)
           root.extents[0].start_block);


    printf("extent[0] blocks: %u\n",
           root.extents[0].block_count);



    if (root.extents[0].block_count)
    {
        dump_root_directory(fp,
                            root.extents[0].start_block);
    }


    fclose(fp);

    return 0;
}
