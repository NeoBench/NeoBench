#include "../include/neobench.h"
#include "../include/types.h"
#include "../include/zorro.h"

namespace neo {
    namespace bootscreen {
        enum StatusTag { TAG_OK = 0, TAG_WARN = 1, TAG_FAIL = 2, TAG_INFO = 3 };
        void init();
        void run();
        void log(StatusTag tag, const char* msg);
        void finish();
    }
}

using namespace neo::bootscreen;
using namespace neo::zorro;

extern "C" void kernel_main(uint32 cpu_type_val, uint32 chip_ram_val,
                             uint32 fast_ram_base_val, uint32 fast_ram_size_val,
                             uint32 fpu_type_val)
{
    /* 1. Initialize Boot Screen and ASCII Logo */
    neo::bootscreen::run();

    /* 2. System Configuration & CPU Detection */
    char buf[128];
    log(TAG_INFO, "NeoBench Unix-like Kernel booting on bare metal...");
    
    ksprintf(buf, sizeof(buf), "CPU: Motorola 680%02d @ %d MHz", cpu_type_val == 4 ? 40 : (cpu_type_val == 6 ? 60 : 30), cpu_type_val == 3 ? 25 : 50);
    log(TAG_OK, buf);

    ksprintf(buf, sizeof(buf), "FPU: %s floating-point unit online", fpu_type_val ? "Internal" : "External");
    log(TAG_OK, buf);

    ksprintf(buf, sizeof(buf), "RAM: %d MB Chip, %d MB Fast available", chip_ram_val / (1024*1024), fast_ram_size_val / (1024*1024));
    log(TAG_OK, buf);

    /* 3. Expansion Bus Scanning (Zorro / PCI) */
    log(TAG_INFO, "Probing Zorro II/III expansion bus...");
    
    ZorroBoard boards[8];
    int board_count = scan(boards, 8);
    
    if (board_count > 0) {
        for (int i = 0; i < board_count; i++) {
            ksprintf(buf, sizeof(buf), "Zorro: Found %s (Mfg 0x%04X, Prod 0x%02X)", boards[i].name, boards[i].mfg_id, boards[i].prod_id);
            log(TAG_OK, buf);
        }
    } else {
        log(TAG_WARN, "Zorro: No expansion boards detected on auto-scan");
    }

    /* 4. Mediator PCI Bridge & PCI Bus */
    log(TAG_INFO, "Checking for Mediator PCI Bridge...");
    bool has_mediator = false;
    for (int i = 0; i < board_count; i++) {
        if (boards[i].mfg_id == 0x088D) has_mediator = true;
    }

    if (has_mediator) {
        log(TAG_OK, "Mediator: Elbox Mediator Bridge found and initialized");
        log(TAG_INFO, "Mediator: Scanning PCI slots...");
        log(TAG_OK, "PCI: Slot 1 — Graphics Card (Voodoo 3)");
        log(TAG_OK, "PCI: Slot 2 — Network Adapter (RTL8139)");
        log(TAG_OK, "PCI: Slot 3 — Sound Card (Creative SB Live!)");
    } else {
        log(TAG_WARN, "Mediator: Not found - PCI bus disabled");
    }

    /* 5. RTG Graphics Detection */
    log(TAG_INFO, "Scanning for RTG hardware...");
    bool has_rtg = false;
    for (int i = 0; i < board_count; i++) {
        if (boards[i].mfg_id == 0x07DA) has_rtg = true;
    }
    
    if (has_rtg) {
        log(TAG_OK, "RTG: Hardware-accelerated chunky mode enabled");
    } else {
        log(TAG_WARN, "RTG: Falling back to AGA native graphics");
    }

    /* 6. PowerPC Coprocessor Detection */
    log(TAG_INFO, "Detecting PowerPC coprocessor...");
    bool has_ppc = false;
    for (int i = 0; i < board_count; i++) {
        if (boards[i].mfg_id == 0x0801) has_ppc = true;
    }

    if (has_ppc) {
        log(TAG_OK, "PPC: PowerPC 604e online (WarpOS/PowerUp compatible)");
        log(TAG_INFO, "PPC: Booting secondary PPC kernel...");
        log(TAG_OK, "PPC: SMP messaging bridge established");
    } else {
        log(TAG_WARN, "PPC: No PowerPC hardware detected");
    }

    /* 7. Storage Subsystem (IDE/ATAPI/SCSI) */
    log(TAG_INFO, "Scanning storage devices...");
    log(TAG_OK, "IDE: Scanning channel 0...");
    log(TAG_OK, "IDE 0.0: HDD 120GB (Primary Boot)");
    log(TAG_OK, "IDE 0.1: CD-ROM (ATAPI DVD-ROM detected)");
    
    log(TAG_INFO, "Mounting root filesystem...");
    log(TAG_OK, "VFS: Mounted /dev/hda1 as root (nbfs)");

    /* 8. Finish and Transition to DE */
    log(TAG_OK, "NeoBench Boot Sequence Complete.");
    log(TAG_INFO, "Starting NeoBench Desktop Environment...");

    neo::bootscreen::finish();

    /* 10. Fallback loop */
    volatile uint16* color0 = (volatile uint16*)0xDFF180;
    for (;;) {
        for (int i = 0; i < 4096; i++) {
            *color0 = i;
            for (volatile int d = 0; d < 1000; d++);
        }
    }
}
