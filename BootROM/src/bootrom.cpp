// NeoBench BootROM Main Entry (Minimal Working Skeleton)
// Modular, safe, and ready for expansion

// --- Optional: Uncomment if you want a splash image ---
#include "splash_planes.h"

#define CUSTOM   ((volatile unsigned short *)0xDFF000)
#define COLOR00  CUSTOM[0x180 >> 1]

// --- Boot device constants ---
enum {
    DEV_MEDIATOR_SCSI,
    DEV_SD,
    DEV_USB,
    DEV_PCI,
    DEV_ZORRO,
    DEV_COUNT
};

// --- Forward declarations ---
void show_splash();
void bus_scan();
int  bootmenu();
bool try_boot(int dev);

// --- Main ROM Entry ---
extern "C" void _start() {
    // 1. Prove ROM runs: green border
    COLOR00 = 0x0F0; // Green

    // 2. Optional: show splash (currently a stub)
    show_splash();

    // 3. Scan all buses (currently a stub)
    bus_scan();

    // 4. Boot device menu (currently a stub)
    int bootdev = bootmenu();

    // 5. Try to boot from device (currently always fails)
    bool booted = try_boot(bootdev);

    // 6. Loop on failure: red/black blinking
    while (!booted) {
        COLOR00 = 0xF00; // Red
        for (volatile unsigned long i = 0; i < 200000; ++i) {}
        COLOR00 = 0x000; // Black
        for (volatile unsigned long i = 0; i < 200000; ++i) {}
    }

    // 7. On boot success, stay green (debug)
    while (1) { COLOR00 = 0x0F0; }
}

// --- Stub: Splash image (for now, just a short delay) ---
void show_splash() {
    COLOR00 = 0x0FF; // Cyan (for debug)
    // TODO: add bitplane image setup here
    for (volatile unsigned long i = 0; i < 300000; ++i) {}
}

// --- Stub: Hardware bus scan (for now, just a short delay) ---
void bus_scan() {
    COLOR00 = 0x00A; // Blue (for debug)
    // TODO: scan PCI, Zorro, SCSI, etc.
    for (volatile unsigned long i = 0; i < 300000; ++i) {}
}

// --- Stub: Boot menu (for now, always selects SCSI) ---
int bootmenu() {
    COLOR00 = 0xEE0; // Yellow (for debug)
    // TODO: add keyboard/copper menu
    for (volatile unsigned long i = 0; i < 300000; ++i) {}
    return DEV_MEDIATOR_SCSI;
}

// --- Stub: Boot from device (for now, always fails) ---
bool try_boot(int dev) {
    COLOR00 = 0xF00; // Red (for debug)
    // TODO: implement device boot logic
    for (volatile unsigned long i = 0; i < 300000; ++i) {}
    return false;
}
