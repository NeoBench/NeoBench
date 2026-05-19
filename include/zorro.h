#ifndef ZORRO_H
#define ZORRO_H

#include "types.h"

/* Zorro Bus Types */
#define ZORRO_BUS_II  2
#define ZORRO_BUS_III 3

/* 
 * ZorroDevice - Represents a physical board found on the bus
 */
typedef struct {
    uint16_t mfg_id;
    uint8_t  prod_id;
    uint32_t base;
    uint32_t size;
    uint8_t  bus_type;
    uint8_t  slot;
    const char* name;
    void*    driver_data; /* Reserved for the attached driver */
} ZorroDevice;

/*
 * ZorroDriver - Structure that drivers fill out to register themselves
 */
typedef struct ZorroDriver {
    const char* name;
    uint16_t    mfg_id;
    uint8_t     prod_id;
    
    /* Returns true if the driver wants to claim this device */
    bool (*match)(ZorroDevice* dev);
    
    /* Called to initialize the driver for this device */
    bool (*attach)(ZorroDevice* dev);
    
    struct ZorroDriver* next;
} ZorroDriver;

/* Kernel API */
void zorro_init(void);
int  zorro_scan(void);
void zorro_register_driver(ZorroDriver* driver);

/* Device access */
ZorroDevice* zorro_get_device(int index);
int          zorro_get_device_count(void);

#endif
