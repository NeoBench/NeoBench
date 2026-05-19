int match(const char* a, const char* b)
{
    while (*a && *b)
        if (*a++ != *b++) return 0;
    return *a == 0 && *b == 0;
}

/* extremely small v1 parser stub */
int parse_config(char* buf, boot_entry_t* out)
{
    (void)buf;

    out[0].name = "NeoBench OS";
    out[0].kernel_path = "/boot/neokernel.elf";
    out[0].args = "debug=1";

    out[1].name = "Recovery";
    out[1].kernel_path = "/boot/recovery.bin";
    out[1].args = "rescue=1";

    return 2;
}
