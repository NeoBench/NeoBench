/**
 * NeoBench BIOS Interface
 *
 * This file contains the code for the BIOS setup interface,
 * providing configuration options for the boot ROM.
 */

#include <stdint.h>

// BIOS configuration structure - stored in NVRAM
typedef struct {
    uint32_t signature;           // BIOS signature "NBOS"
    uint32_t boot_priority;       // Boot device priority
    uint8_t  quick_boot;          // Skip POST tests flag
    uint8_t  boot_logo;           // Show boot logo flag
    uint8_t  boot_sound;          // Play boot sound flag
    uint32_t memory_test;         // Memory test level (0=none, 1=quick, 2=full)
    uint8_t  reserved[16];        // Reserved for future use
} BiosConfig;

// Default BIOS configuration
static const BiosConfig DEFAULT_CONFIG = {
    .signature = 0x4E424F53,      // "NBOS"
    .boot_priority = 0x00010200,  // USB(2), SCSI(1), Floppy(0)
    .quick_boot = 1,              // Quick boot enabled
    .boot_logo = 1,               // Show boot logo
    .boot_sound = 1,              // Play boot sound
    .memory_test = 1,             // Quick memory test
    .reserved = {0}
};

// Current configuration
static BiosConfig current_config;

// Function prototypes
void bios_interface_init(void);
int bios_load_config(void);
int bios_save_config(void);
void bios_reset_defaults(void);
void bios_setup_interface(void);

/**
 * Initialize the BIOS interface
 */
void bios_interface_init(void) {
    // Try to load configuration from NVRAM
    if (!bios_load_config()) {
        // If loading fails, use defaults
        bios_reset_defaults();
    }
}

/**
 * Load BIOS configuration from NVRAM
 * Returns 1 on success, 0 on failure
 */
int bios_load_config(void) {
    // In a real implementation, this would read from NVRAM or EEPROM
    // For this simulation, we'll pretend to load from a fixed memory location
    
    // Check if config is valid (has correct signature)
    if (current_config.signature == 0x4E424F53) {
        return 1;  // Config loaded successfully
    }
    
    return 0;  // Config not valid
}

/**
 * Save BIOS configuration to NVRAM
 * Returns 1 on success, 0 on failure
 */
int bios_save_config(void) {
    // In a real implementation, this would write to NVRAM or EEPROM
    current_config.signature = 0x4E424F53;  // Set valid signature
    return 1;  // Pretend save was successful
}

/**
 * Reset BIOS configuration to defaults
 */
void bios_reset_defaults(void) {
    current_config = DEFAULT_CONFIG;
    bios_save_config();
}

/**
 * Display and handle the BIOS setup interface
 */
void bios_setup_interface(void) {
    // In a real implementation, this would display menus and handle user input
    
    // Main menu structure
    const char* main_menu[] = {
        "System Information",
        "Boot Options",
        "Advanced Settings",
        "Save and Exit",
        "Exit Without Saving",
        "Load Defaults"
    };
    
    // Boot options menu structure
    const char* boot_menu[] = {
        "Boot Priority",
        "Quick Boot",
        "Boot Logo",
        "Boot Sound"
    };
    
    // For the ROM simulation, we just pretend to display menus
    // The actual implementation would draw these on screen and handle input
}

/**
 * Get current boot priority configuration
 */
uint32_t bios_get_boot_priority(void) {
    return current_config.boot_priority;
}

/**
 * Set boot priority configuration
 */
void bios_set_boot_priority(uint32_t priority) {
    current_config.boot_priority = priority;
}

/**
 * Configure boot device order
 * device_type: 0=Floppy, 1=SCSI, 2=USB
 * position: 0=First, 1=Second, 2=Third
 */
void bios_set_boot_device_order(int device_type, int position) {
    uint32_t priority = current_config.boot_priority;
    uint32_t new_priority = 0;
    
    // Extract current values
    int first = (priority >> 16) & 0xFF;
    int second = (priority >> 8) & 0xFF;
    int third = priority & 0xFF;
    
    // Set new position
    if (position == 0) {
        first = device_type;
    } else if (position == 1) {
        second = device_type;
    } else if (position == 2) {
        third = device_type;
    }
    
    // Create new priority value
    new_priority = (first << 16) | (second << 8) | third;
    current_config.boot_priority = new_priority;
}