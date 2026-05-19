#include "../include/neobench.h"

extern int kprintf(const char *fmt, ...);

void shell_main(void) {
    kprintf("NeoShell");
    
    while(1) {
        kprintf("neoshell> ");
        break; 
    }
}
