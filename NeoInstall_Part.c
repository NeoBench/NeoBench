#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 
 * NeoBench Partition Installer (for mounted DH0:)
 * Writes relative to partition start.
 */

int main(int argc, char **argv) {
    FILE *fh, *kf, *bf;
    unsigned char *buffer;
    unsigned char *kbuf;
    
    printf("NeoBench Partition Installer v1.2\n");
    
    if (argc < 3) {
        printf("Usage: NeoInstall_Part [Partition:] [Kernel] [Bootblock]\n");
        printf("Example: NeoInstall_Part DH0: build/neobench.bin build/bootblock.bin\n");
        return 10;
    }
    
    buffer = malloc(1024);
    kbuf = malloc(262144);
    if (!buffer || !kbuf) {
        printf("Out of memory\n");
        return 20;
    }

    /* Open the PARTITION device (e.g. DH0:) */
    fh = fopen(argv[1], "r+b");
    if (!fh) {
        printf("Error: Could not open %s. Is it mounted?\n", argv[1]);
        return 20;
    }
    
    /* 1. Install Bootblock to logical Block 0 & 1 of the partition */
    if (argc > 3) {
        bf = fopen(argv[3], "rb");
        if (bf) {
            memset(buffer, 0, 1024);
            fread(buffer, 1, 1024, bf);
            fclose(bf);
            fseek(fh, 0, SEEK_SET);
            if (fwrite(buffer, 1, 1024, fh) == 1024)
                printf("Bootblock installed to %s sectors 0-1\n", argv[1]);
            else
                printf("Error writing bootblock!\n");
        }
    }
    
    /* 2. Install Kernel to logical Block 2 of the partition */
    kf = fopen(argv[2], "rb");
    if (kf) {
        memset(kbuf, 0, 262144);
        fread(kbuf, 1, 262144, kf);
        fclose(kf);
        /* In this layout, Kernel follows bootblock immediately at partition offset */
        fseek(fh, 1024, SEEK_SET); 
        if (fwrite(kbuf, 1, 262144, fh) == 262144)
            printf("Kernel installed to %s sectors 2+\n", argv[1]);
        else
            printf("Error writing kernel!\n");
    }
    
    fclose(fh);
    free(buffer);
    free(kbuf);
    printf("Installation complete. Ensure partition is set to Bootable in HDToolBox.\n");
    return 0;
}
