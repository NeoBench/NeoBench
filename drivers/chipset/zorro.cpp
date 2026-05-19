#include "zorro.h"
#include "neobench.h"

namespace neo {
namespace zorro {

static constexpr uint32_t Z2_CONFIG_BASE = 0xE80000;
static constexpr uint32_t Z3_CONFIG_BASE = 0xFF000000;

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

const char* lookup_board_name(uint16_t mfg, uint8_t prod) {
    if (mfg == 0x07DA) { // Village Tronic
        if (prod == 0x0B) return "Picasso II";
        if (prod == 0x15) return "Picasso IV";
    }
    if (mfg == 0x088D) { // Elbox
        if (prod == 0x01) return "Mediator 1200";
        if (prod == 0x02) return "Mediator 4000";
    }
    if (mfg == 0x0801) { // Phase 5
        if (prod == 0x01) return "Cyberstorm PPC";
        if (prod == 0x02) return "Blizzard PPC";
    }
    return "Unknown Board";
}

int scan(ZorroBoard* boards, int max_boards) {
    int count = 0;
    // Simplified scan for Zorro II
    // In a real Amiga, you have to configure each board to see the next.
    // This is just a stub for detection representation.
    
    uint16_t mfg = ac_read_mfg(Z2_CONFIG_BASE);
    if (mfg != 0xFFFF && mfg != 0x0000) {
        boards[count].mfg_id = mfg;
        boards[count].prod_id = ac_read_prod(Z2_CONFIG_BASE);
        boards[count].name = lookup_board_name(mfg, boards[count].prod_id);
        count++;
    }
    
    return count;
}

} // namespace zorro
} // namespace neo
