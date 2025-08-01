/**
 * NeoBench Multiboot Module
 *
 * This module provides unified boot support for SCSI, Floppy, and USB devices
 * with a configurable boot menu and device priority system.
 */

#include <stdint.h>

// Boot device types
#define BOOT_DEV_NONE   0
#define BOOT_DEV_FLOPPY 1
#define BOOT_DEV_SCSI   2
#define BOOT_DEV_USB    3

// Boot device information structure
typedef struct {
    uint8_t  type;              // Device type (BOOT_DEV_*)
    uint8_t  id;                // Device ID/index
    uint8_t  bootable;          // 1 if bootable, 0 if not
    uint8_t  active;            // 1 if device is active
    char     label[16];         // Device label (null-terminated)
    uint32_t size;              // Size in MB/KB (device dependent)
} BootDevice;

// Boot configuration
typedef struct {
    uint32_t signature;         // Configuration signature "BOOT"
    uint8_t  priority[4];       // Boot priority (by device type)
    uint8_t  timeout;           // Boot menu timeout in seconds
    uint8_t  show_menu;         // 1 to always show menu, 0 for auto-boot
    uint8_t  verbose;           // Verbose boot messages
    uint8_t  quick_boot;        // Skip hardware tests
} BootConfig;

// Global variables
static BootDevice boot_devices[16];
static uint8_t boot_device_count = 0;
static BootConfig boot_config;

// Forward declarations
void multiboot_init(void);
int multiboot_detect_devices(void);
void multiboot_sort_devices(void);
int multiboot_boot_from_device(BootDevice* device);
void multiboot_show_menu(void);
void multiboot_error(const char* message);

/**
 * Initialize the multiboot system
 */
void multiboot_init(void) {
    // Initialize boot device array
    for (int i = 0; i < 16; i++) {
        boot_devices[i].type = BOOT_DEV_NONE;
        boot_devices[i].bootable = 0;
        boot_devices[i].active = 0;
    }
    
    // Set default boot configuration
    boot_config.signature = 0x424F4F54; // "BOOT"
    boot_config.priority[0] = BOOT_DEV_SCSI;  // First priority
    boot_config.priority[1] = BOOT_DEV_USB;   // Second priority
    boot_config.priority[2] = BOOT_DEV_FLOPPY;// Third priority
    boot_config.priority[3] = BOOT_DEV_NONE;  // Not used
    boot_config.timeout = 3;                  // 3 seconds
    boot_config.show_menu = 0;                // Auto-boot
    boot_config.verbose = 1;                  // Show messages
    boot_config.quick_boot = 1;               // Fast boot
    
    // Reset boot device counter
    boot_device_count = 0;
}

/**
 * Detect all bootable devices
 * Returns the number of bootable devices found
 */
int multiboot_detect_devices(void) {
    // In real implementation, this would call hardware-specific detection
    // routines for each device type
    
    // For simulation, we'll add a few sample devices
    
    // SCSI hard drive
    boot_devices[boot_device_count].type = BOOT_DEV_SCSI;
    boot_devices[boot_device_count].id = 0;
    boot_devices[boot_device_count].bootable = 1;
    boot_devices[boot_device_count].active = 1;
    boot_devices[boot_device_count].size = 2048;  // 2GB
    for (int i = 0; i < 16; i++) {
        boot_devices[boot_device_count].label[i] = "SCSI HDD"[i];
        if (i >= 8) break;
    }
    boot_device_count++;
    
    // USB flash drive
    boot_devices[boot_device_count].type = BOOT_DEV_USB;
    boot_devices[boot_device_count].id = 0;
    boot_devices[boot_device_count].bootable = 1;
    boot_devices[boot_device_count].active = 1;
    boot_devices[boot_device_count].size = 4096;  // 4GB
    for (int i = 0; i < 16; i++) {
        boot_devices[boot_device_count].label[i] = "USB Flash"[i];
        if (i >= 9) break;
    }
    boot_device_count++;
    
    // Floppy drive A:
    boot_devices[boot_device_count].type = BOOT_DEV_FLOPPY;
    boot_devices[boot_device_count].id = 0;
    boot_devices[boot_device_count].bootable = 1;
    boot_devices[boot_device_count].active = 1;
    boot_devices[boot_device_count].size = 1440;  // 1.44MB
    for (int i = 0; i < 16; i++) {
        boot_devices[boot_device_count].label[i] = "Floppy A:"[i];
        if (i >= 9) break;
    }
    boot_device_count++;
    
    return boot_device_count;
}

/**
 * Sort boot devices according to configured priority
 */
void multiboot_sort_devices(void) {
    // Simple bubble sort by priority
    for (int i = 0; i < boot_device_count - 1; i++) {
        for (int j = 0; j < boot_device_count - i - 1; j++) {
            // Get priority for current device
            uint8_t prio_j = 4;  // Default to lowest priority
            for (int p = 0; p < 4; p++) {
                if (boot_config.priority[p] == boot_devices[j].type) {
                    prio_j = p;
                    break;
                }
            }
            
            // Get priority for next device
            uint8_t prio_j1 = 4;  // Default to lowest priority
            for (int p = 0; p < 4; p++) {
                if (boot_config.priority[p] == boot_devices[j+1].type) {
                    prio_j1 = p;
                    break;
                }
            }
            
            // Swap if needed
            if (prio_j > prio_j1) {
                BootDevice temp = boot_devices[j];
                boot_devices[j] = boot_devices[j+1];
                boot_devices[j+1] = temp;
            }
        }
    }
}

/**
 * Attempt to boot from a specific device
 * Returns 1 if successful, 0 if failed
 */
int multiboot_boot_from_device(BootDevice* device) {
    if (!device || !device->bootable || !device->active) {
        return 0;
    }
    
    // In a real implementation, this would:
    // 1. Select the appropriate device controller
    // 2. Read the boot sector from the device
    // 3. Verify boot signature
    // 4. Transfer control to the boot sector
    
    // For each device type, call the appropriate boot routine
    switch (device->type) {
        case BOOT_DEV_FLOPPY:
            // Boot from floppy drive
            // floppy_boot(device->id);
            break;
            
        case BOOT_DEV_SCSI:
            // Boot from SCSI device
            // scsi_boot(device->id);
            break;
            
        case BOOT_DEV_USB:
            // Boot from USB device
            // usb_boot(device->id);
            break;
            
        default:
            return 0;  // Unknown device type
    }
    
    return 0;  // Boot failed
}

/**
 * Display the boot device selection menu
 */
void multiboot_show_menu(void) {
    // In a real implementation, this would:
    // 1. Display a menu of bootable devices
    // 2. Allow user to select a device with keyboard
    // 3. Set a timeout for auto-selection
    
    // For simulation, we'll just print the device list
    /*
    printf("NeoBench Boot Menu\n");
    printf("-----------------\n\n");
    
    for (int i = 0; i < boot_device_count; i++) {
        if (boot_devices[i].bootable && boot_devices[i].active) {
            printf("%d. %s\n", i+1, boot_devices[i].label);
        }
    }
    
    printf("\nPress 1-%d to select device, or Enter for default\n", boot_device_count);
    */
}

/**
 * Display a boot error message
 */
void multiboot_error(const char* message) {
    // In a real implementation, this would display the error on screen
    // and possibly wait for user input
    /*
    printf("Boot Error: %s\n", message);
    printf("Press F2 for setup, or any key to retry\n");
    */
}