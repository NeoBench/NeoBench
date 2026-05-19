/*
 * NeoBench Bare-Metal Amiga Kernel
 * Mouse Driver
 *
 * Reads mouse position from JOY0DAT and button state from CIA-A PRA
 * and POTGOR. Tracks absolute position with configurable bounds.
 *
 * Corrections vs v1.0:
 *
 *  1. POTGOR SETUP WRONG (critical for right/middle button reading).
 *     POTGOR (0xDFF034) controls the POT pins used for right/middle buttons.
 *     To read the buttons, you must:
 *       - Set POTGOR bits [15:14] = 11 (enable output on DATS/DATL for port 0)
 *       - Set POTGOR bits [9,7] = 11 (output-enable for port 0 DAT pins)
 *       Actually the correct setup to read mouse buttons is:
 *       POTGOR = 0x0000 disables POT counters entirely which is WRONG;
 *       it means the pins float.
 *       The correct value to enable reading of port 0 right/middle buttons:
 *         POTGOR = 0xFF00
 *       Bit 15: OUTRY=1 (output enable, right button port 1)
 *       Bit 14: DATRY=1 (data: set high to allow reading via POTGOR)
 *       Bit 13: OUTRY1=1
 *       Bit 12: DATRY1=1 (port 1 - not used for mouse but set high)
 *       Bit 9:  OUTRX=1 (output enable right button port 0) -- wait,
 *       Let me be precise from the HRM:
 *         POTGOR bits:
 *           15: START    - starts POT counters
 *           14: DATEN    - enable POT counter output
 *           11: OUTRY    - output enable for port 1 button 2 (middle)
 *           10: DATRY    - data for port 1 button 2
 *            9: OUTRX    - output enable for port 0 button 2 (right mouse)
 *            8: DATRX    - data for port 0 button 2
 *            7: OUTLY    - output enable for port 1 button 3 (middle? no)
 *            6: DATLY
 *            5: OUTLX    - output enable for port 0 button 3 (middle mouse)
 *            4: DATLX    - data for port 0 button 3
 *       To READ right button (port 0): set OUTRX=1, DATRX=1.
 *       To READ middle button (port 0): set OUTLX=1, DATLX=1.
 *       Correct POTGOR value: 0x0300 (OUTRX=1, DATRX=1) for right button.
 *       For middle button too: 0x0330 (adds OUTLX=1, DATLX=1).
 *       We write 0xFF00 to be safe (all output-enables and data high).
 *
 *  2. POTGOR BUTTON BIT POSITIONS WRONG.
 *     The original used:
 *       RMB_MASK = 0x0400 (bit 10) -- this is DATRY (port 1 right) NOT port 0!
 *       MMB_MASK = 0x0100 (bit 8)  -- this is DATRX (port 0 right) = right button
 *     The correct POTGOR bits for port 0 (mouse port, right connector):
 *       Bit 10 = DATRY = port 1 right button (joystick port, LEFT connector)
 *       Bit  8 = DATRX = port 0 right button (mouse port, RIGHT connector) <- RMB
 *       Bit  4 = DATLX = port 0 middle button (mouse port)                 <- MMB
 *     So the original had RMB and MMB swapped, and was actually reading
 *     the JOYSTICK port's button for RMB.
 *     Fixed: RMB_MASK = 0x0100 (bit 8 = DATRX), MMB_MASK = 0x0010 (bit 4 = DATLX).
 *
 *  3. Y AXIS POLARITY.
 *     The original applied dy directly: pos_y += dy.
 *     On the Amiga, moving the mouse UP decreases the Y counter (the counter
 *     decrements when moving up).  In screen coordinates, moving up = lower
 *     Y value, so the polarity is CORRECT as-is for screen-space tracking.
 *     No change needed here.
 *
 *  4. INCLUDE PATH.
 *     The original used "../chipset/custom.h" but with the Makefile's
 *     -Idrivers/chipset flag, "custom.h" is sufficient.  Fixed.
 *
 *  5. POTGOR IS AT 0xDFF034, WHICH MATCHES THE CUSTOM.H POTGO_REG = 0x034.
 *     The original used a local constexpr POTGO_REG = 0x034 and then called
 *     custom_write(POTGO_REG, ...) which is correct since custom_write adds
 *     CUSTOM_BASE (0xDFF000).  No bug here, but we use the named constant
 *     from custom.h now that the include is fixed.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "custom.h"

namespace neo {
namespace mouse {

/* ========================================================================
 * Hardware details
 *
 * JOY0DAT (read via custom_read(JOY0DAT)):
 *   Bits [15:8] = Y quadrature counter (signed delta when cast to int8_t)
 *   Bits  [7:0] = X quadrature counter
 *
 * CIA-A PRA (0xBFE001):
 *   Bit 6 = left mouse button (active LOW: 0 = pressed)
 *
 * POTGOR / POTGOR:
 *   Write POTGOR to configure; read POTGOR to get current state.
 *   For mouse (port 0, right connector):
 *     Right button  = POTGOR bit 8  (DATRX, active LOW)
 *     Middle button = POTGOR bit 4  (DATLX, active LOW)
 *   Setup: write POTGOR = 0xFF00 (output-enable all, data all high).
 * ======================================================================== */

static constexpr uint32_t CIAA_PRA_ADDR = 0xBFE001UL;
static constexpr uint8_t  LMB_BIT       = 0x40;    /* CIA-A PRA bit 6 */
static constexpr uint16_t RMB_BIT       = 0x0100;  /* POTGOR bit 8  (DATRX) */
static constexpr uint16_t MMB_BIT       = 0x0010;  /* POTGOR bit 4  (DATLX) */

/* Public button flags */
static constexpr uint8_t BTN_LEFT       = 0x01;
static constexpr uint8_t BTN_RIGHT      = 0x02;
static constexpr uint8_t BTN_MIDDLE     = 0x04;

/* ========================================================================
 * State
 * ======================================================================== */

static int32_t pos_x, pos_y;
static int32_t min_x, max_x, min_y, max_y;
static uint8_t prev_x_counter;
static uint8_t prev_y_counter;
static uint8_t buttons;

/* ========================================================================
 * Public API
 * ======================================================================== */

void init(int32_t screen_width, int32_t screen_height)
{
    min_x = 0;
    min_y = 0;
    max_x = screen_width  - 1;
    max_y = screen_height - 1;
    pos_x = screen_width  / 2;
    pos_y = screen_height / 2;

    uint16_t joy0dat = custom_read(JOY0DAT);
    prev_x_counter   = (uint8_t)(joy0dat & 0xFF);
    prev_y_counter   = (uint8_t)(joy0dat >> 8);

    buttons = 0;

    /*
     * Configure POTGOR for reading mouse buttons (port 0).
     * Write 0xFF00: sets output-enable and data-high for all POT pins.
     * This puts all POT output pins high, making the buttons readable
     * as active-low signals via POTGOR.
     */
    custom_write(POTGOR, 0xFF00);

    return;
}

void set_bounds(int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max)
{
    min_x = x_min;  max_x = x_max;
    min_y = y_min;  max_y = y_max;

    if (pos_x < min_x) pos_x = min_x;
    if (pos_x > max_x) pos_x = max_x;
    if (pos_y < min_y) pos_y = min_y;
    if (pos_y > max_y) pos_y = max_y;
}

void update(void)
{
    /* Read JOY0DAT counters */
    uint16_t joy0dat = custom_read(JOY0DAT);
    uint8_t  cur_x   = (uint8_t)(joy0dat & 0xFF);
    uint8_t  cur_y   = (uint8_t)(joy0dat >> 8);

    /* Signed 8-bit subtraction handles counter wrap-around correctly */
    int8_t dx = (int8_t)(cur_x - prev_x_counter);
    int8_t dy = (int8_t)(cur_y - prev_y_counter);

    prev_x_counter = cur_x;
    prev_y_counter = cur_y;

    pos_x += (int32_t)dx;
    pos_y += (int32_t)dy;

    if (pos_x < min_x) pos_x = min_x;
    if (pos_x > max_x) pos_x = max_x;
    if (pos_y < min_y) pos_y = min_y;
    if (pos_y > max_y) pos_y = max_y;

    /* Left button: CIA-A PRA bit 6, active low */
    uint8_t pra = *((volatile const uint8_t *)CIAA_PRA_ADDR);
    buttons = 0;
    if (!(pra & LMB_BIT)) buttons |= BTN_LEFT;

    /* Right and middle buttons: POTGOR, active low */
    uint16_t potgor = custom_read(POTGOR);
    if (!(potgor & RMB_BIT)) buttons |= BTN_RIGHT;
    if (!(potgor & MMB_BIT)) buttons |= BTN_MIDDLE;
}

void get_position(int32_t *x, int32_t *y) { *x = pos_x; *y = pos_y; }
uint8_t get_buttons(void)                  { return buttons; }
bool left_pressed(void)    { return (buttons & BTN_LEFT)   != 0; }
bool right_pressed(void)   { return (buttons & BTN_RIGHT)  != 0; }
bool middle_pressed(void)  { return (buttons & BTN_MIDDLE) != 0; }

void set_position(int32_t x, int32_t y)
{
    pos_x = x;
    pos_y = y;
    if (pos_x < min_x) pos_x = min_x;
    if (pos_x > max_x) pos_x = max_x;
    if (pos_y < min_y) pos_y = min_y;
    if (pos_y > max_y) pos_y = max_y;
}

} /* namespace mouse */
} /* namespace neo */
