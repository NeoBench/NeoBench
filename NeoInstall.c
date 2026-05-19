#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *fh, *kf, *bf;
    unsigned char *buffer;
    unsigned char *kbuf;
    
    printf("NeoBench Amiga Installer v1.1\n");
    
    if (argc < 4) {
        printf("Usage: NeoInstall [HDF_File] [Kernel] [Bootblock]\n");
        return 10;
    }
    
    buffer = malloc(1024);
    kbuf = malloc(262144);
    if (!buffer || !kbuf) {
        printf("Out of memory\n");
        return 20;
    }

    fh = fopen(argv[1], "r+b");
    if (!fh) {
        printf("Error: Could not open %s\n", argv[1]);
        return 20;
    }
    
    bf = fopen(argv[3], "rb");
    if (bf) {
        memset(buffer, 0, 1024);
        fread(buffer, 1, 1024, bf);
        fclose(bf);
        fseek(fh, 2016 * 512, SEEK_SET);
        fwrite(buffer, 1, 1024, fh);
        printf("Bootblock installed.\n");
    }
    
    kf = fopen(argv[2], "rb");
    if (kf) {
        memset(kbuf, 0, 262144);
        fread(kbuf, 1, 262144, kf);
        fclose(kf);
        fseek(fh, 2018 * 512, SEEK_SET);
        fwrite(kbuf, 1, 262144, fh);
        printf("Kernel installed.\n");
    }
    
    fclose(fh);
    free(buffer);
    free(kbuf);
    printf("Done.\n");
    return 0;
}
