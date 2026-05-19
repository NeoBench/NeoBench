/*
 * NeoBench Kernel - Main Entry Point (C99)
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "../include/zorro.h"
#include "../include/interrupts.h"
#include "../include/timer.h"

/* Status Tags for Bootscreen */
typedef enum { TAG_OK = 0, TAG_WARN = 1, TAG_FAIL = 2, TAG_INFO = 3 } StatusTag;

/* Forward declarations of C-style bootscreen API */
void neo_bootscreen_run(void);
void neo_bootscreen_log(StatusTag tag, const char* msg);
void neo_bootscreen_finish(void);

/* Mocking ksprintf for now - needs proper printf implementation */
int ksprintf(char* buf, int size, const char* fmt, ...);

void kernel_main(uint32 cpu_type_val, uint32 chip_ram_val,
                 uint32 fast_ram_base_val, uint32 fast_ram_size_val,
                 uint32 fpu_type_val)
{
    /* 1. Initialize Boot Screen */
    neo_bootscreen_run();

    /* 2. Initialize Interrupts and Timer */
    intr_init();
    timer_init();
    intr_enable();
    
    neo_bootscreen_log(TAG_OK, "Interrupts and System Timer initialized");

    /* 3. Initialize Expansion Bus */
    rtg_init(); /* Registers RTG drivers */
    zorro_init();
    zorro_scan();
    
    neo_bootscreen_log(TAG_OK, "Zorro Bus scanned");

    /* 3. Log CPU/Memory Info */
    char buf[128];
    ksprintf(buf, sizeof(buf), "CPU: Motorola 680%d detected", cpu_type_val == 4 ? 40 : 30);
    neo_bootscreen_log(TAG_OK, buf);

    /* ... rest of logic ... */
    
    /* 4. Probe Hardware */
    neo_bootscreen_log(TAG_INFO, "Probing Zorro bus...");
    
    /* Fallback color cycle */
    volatile uint16* color0 = (volatile uint16*)0xDFF180;
    while(1) {
        for (int i = 0; i < 4096; i++) {
            *color0 = i;
        }
    }
}
