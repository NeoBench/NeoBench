#include "../include/zorro.h"
#include "../include/neobench.h"
#include <string.h>

/*
 * NeoBench Bare-Metal Amiga Kernel
 * Zorro Bus Driver - NetBSD-style dynamic discovery
 */

#define Z2_CONFIG_BASE  0x00E80000UL
#define Z3_CONFIG_BASE  0xFF000000UL /* Note: Only on 030+ with Z3 */

#define MAX_ZORRO_DEVICES 32

static ZorroDevice devices[MAX_ZORRO_DEVICES];
static int device_count = 0;
static ZorroDriver* driver_list = 0;

/* 
 * Nibble-based Autoconfig reads 
 * Amiga AutoConfig hardware uses inverted bits and 2-byte spacing.
 */
static uint8_t ac_read_nibble(uint32_t base, uint32_t offset) {
    volatile uint8_t *addr = (volatile uint8_t *)(base + offset);
    return (~(*addr)) >> 4;
}

static uint16_t ac_read_mfg(uint32_t base) {
    uint16_t mfg = 0;
    mfg |= (uint16_t)(ac_read_nibble(base, 0x10) & 0x0F) << 12;
    mfg |= (uint16_t)(ac_read_nibble(base, 0x12) & 0x0F) << 8;
    mfg |= (uint16_t)(ac_read_nibble(base, 0x14) & 0x0F) << 4;
    mfg |= (uint16_t)(ac_read_nibble(base, 0x16) & 0x0F);
    return mfg;
}

static uint8_t ac_read_prod(uint32_t base) {
    uint8_t prod = 0;
    prod |= (ac_read_nibble(base, 0x00) & 0x0F) << 4;
    prod |= (ac_read_nibble(base, 0x02) & 0x0F);
    return prod;
}

/* Hardcoded fallback names for common hardware */
static const char* fallback_board_name(uint16_t mfg, uint8_t prod) {
    if (mfg == 0x07DA) {
        if (prod == 0x0B) return "Picasso II";
        if (prod == 0x15) return "Picasso IV";
    }
    if (mfg == 0x088D) return "Elbox Mediator";
    if (mfg == 0x0801) return "Phase 5 Accelerator";
    return "Unknown Zorro Board";
}

void zorro_register_driver(ZorroDriver* driver) {
    driver->next = driver_list;
    driver_list = driver;
}

/* 
 * Scan the Zorro II configuration space.
 */
static void zorro_scan_ii(void) {
    uint32_t base = Z2_CONFIG_BASE;
    uint16_t mfg = ac_read_mfg(base);
    
    if (mfg != 0xFFFF && mfg != 0x0000) {
        ZorroDevice* dev = &devices[device_count];
        dev->mfg_id = mfg;
        dev->prod_id = ac_read_prod(base);
        dev->bus_type = ZORRO_BUS_II;
        dev->name = fallback_board_name(mfg, dev->prod_id);
        
        ZorroDriver* drv = driver_list;
        while (drv) {
            if (drv->mfg_id == dev->mfg_id && drv->prod_id == dev->prod_id) {
                if (!drv->match || drv->match(dev)) {
                    dev->name = drv->name;
                    if (drv->attach) drv->attach(dev);
                    break;
                }
            }
            drv = drv->next;
        }
        device_count++;
    }
}

/*
 * Scan the Zorro III configuration space.
 */
static void zorro_scan_iii(void) {
    uint32_t base = Z3_CONFIG_BASE;
    uint16_t mfg = ac_read_mfg(base);
    
    if (mfg != 0xFFFF && mfg != 0x0000) {
        ZorroDevice* dev = &devices[device_count];
        dev->mfg_id = mfg;
        dev->prod_id = ac_read_prod(base);
        dev->bus_type = ZORRO_BUS_III;
        dev->name = fallback_board_name(mfg, dev->prod_id);
        
        ZorroDriver* drv = driver_list;
        while (drv) {
            if (drv->mfg_id == dev->mfg_id && drv->prod_id == dev->prod_id) {
                if (!drv->match || drv->match(dev)) {
                    dev->name = drv->name;
                    if (drv->attach) drv->attach(dev);
                    break;
                }
            }
            drv = drv->next;
        }
        device_count++;
    }
}

int zorro_scan(void) {
    device_count = 0;
    zorro_scan_ii();
    zorro_scan_iii();
    return device_count;
}

void zorro_init(void) {
    device_count = 0;
    driver_list = 0;
}

ZorroDevice* zorro_get_device(int index) {
    if (index < 0 || index >= device_count) return 0;
    return &devices[index];
}

int zorro_get_device_count(void) {
    return device_count;
}
