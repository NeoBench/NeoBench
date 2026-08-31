#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "elf_loader.h"

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *image;
    long size;
    neo_elf_info_t info;
    int rc;

    if (argc != 2) {
        fprintf(stderr, "usage: %s kernel.elf\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }

    size = ftell(f);

    if (size < 0) {
        fclose(f);
        return 1;
    }

    rewind(f);

    image = malloc((size_t)size);
    if (!image) {
        fclose(f);
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    if (fread(image, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(image);
        fprintf(stderr, "failed to read ELF\n");
        return 1;
    }

    fclose(f);

    printf("NeoLoader ELF inspection\n");
    printf("------------------------\n");

    rc = neo_elf_load(
        image,
        (uint32_t)size,
        &info
    );

    if (rc != NEO_ELF_OK) {
        printf("ELF validation: FAIL\n");
        printf("Error: %d\n", rc);
        free(image);
        return 1;
    }

    printf("ELF validation: PASS\n");
    printf("Entry:      0x%08x\n", info.entry);
    printf("Image start: 0x%08x\n", info.image_start);
    printf("Image end:   0x%08x\n", info.image_end);
    printf("LOAD count:  %u\n", info.load_count);

    free(image);
    return 0;
}
