#include <stdint.h>

#include "elf_loader.h"
#include "loader.h"
#include "transfer.h"

int neo_load_kernel(const void *image,
                    uint32_t image_size)
{
    neo_elf_info_t info;

    int rc = neo_elf_load_memory(
        image,
        image_size,
        &info
    );

    if (rc != NEO_ELF_OK)
        return rc;

    /*
     * At this point:
     *
     *   PT_LOAD segments have been copied
     *   BSS tails have been cleared
     *   info.entry contains the kernel entry point
     *
     * The current kernel is linked for 0x00001000.
     */
    neo_jump_to_kernel(info.entry);

    return NEOLOADER_OK;
}
