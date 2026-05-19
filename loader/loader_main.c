typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct {
    const char* kernel_path;
    const char* args;
} boot_entry_t;

/* fake placeholders for now */
void load_kernel(const char* path);
void jump_to_kernel(void* entry, void* stack);

void loader_main(void* bootinfo, void* memmap)
{
    (void)bootinfo;
    (void)memmap;

    boot_entry_t entry = {
        .kernel_path = "/boot/neokernel.bin",
        .args = "root=/dev/sda1 debug=1"
    };

    load_kernel(entry.kernel_path);

    void* kernel_entry = (void*)0x100000;  // fixed load addr for v0

    jump_to_kernel(kernel_entry, (void*)0x90000);
}
