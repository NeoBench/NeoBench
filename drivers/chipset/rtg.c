#include "../../include/neobench.h"
#include "../../include/types.h"
#include "../../include/zorro.h"

static bool rtg_match(ZorroDevice* dev) {
    return (dev->mfg_id == 0x07DA && dev->prod_id == 0x0B);
}

static bool rtg_attach(ZorroDevice* dev) {
    /* Initialize Picasso II hardware here */
    dev->driver_data = (void*)0xDEADBEEF;
    return true;
}

static ZorroDriver p2_driver = {
    "Village Tronic Picasso II",
    0x07DA,
    0x0B,
    rtg_match,
    rtg_attach,
    0
};

/* Minimal RTG C99 implementation */
void rtg_init(void) {
    zorro_register_driver(&p2_driver);
}
void rtg_set_mode(int mode) {}
