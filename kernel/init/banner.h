#ifndef NEO_BENCH_BANNER_H
#define NEO_BENCH_BANNER_H

void kernel_banner(void);
void kernel_module_loading(void);
void kernel_module_begin(const char *name);
void kernel_module_ok(void);
void kernel_module_fail(void);
void kernel_module_warn(void);
void kernel_boot_complete(void);

#endif
