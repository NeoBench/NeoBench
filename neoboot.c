#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>

int main() {
    BPTR file;
    void (*kernel)(void);
    unsigned char *mem;
    long size;

    printf("NeoBench Launcher v1.0\n");
    printf("Opening neobench.bin...\n");

    file = Open("neobench.bin", MODE_OLDFILE);
    if (!file) {
        printf("Error: Could not find neobench.bin\n");
        return 20;
    }

    Seek(file, 0, OFFSET_END);
    size = ExamineFH(file); /* Simplified for this example */
    Seek(file, 0, OFFSET_BEGINNING);

    printf("Allocating memory...\n");
    mem = (unsigned char *)AllocMem(0x40000, MEMF_CHIP | MEMF_CLEAR);
    if (!mem) {
        printf("Error: Out of memory\n");
        Close(file);
        return 20;
    }

    printf("Loading kernel at %p...\n", mem);
    Read(file, mem, 0x40000);
    Close(file);

    printf("Taking over hardware. Goodbye AmigaOS!\n");
    
    Forbid();
    Disable();
    
    /* Transition to kernel */
    /* Offset +4 is the Reset PC in our vectors */
    kernel = (void (*)(void))(*(unsigned long *)(mem + 4));
    
    kernel();

    return 0;
}
