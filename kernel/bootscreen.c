#include "../include/neobench.h"
#include "../include/types.h"
#include "../include/gui.h"

/* 
 * NeoBench OS — Boot Screen (C99)
 * Minimal implementation to support linking with VBCC
 */

typedef enum { TAG_OK = 0, TAG_WARN = 1, TAG_FAIL = 2, TAG_INFO = 3 } StatusTag;

void neo_bootscreen_init(void) {
    /* Minimal init */
}

void neo_bootscreen_run(void) {
    /* Minimal run - drawing will be handled by calling log directly */
}

void neo_bootscreen_log(StatusTag tag, const char* msg) {
    /* Minimal log: for now, just print to serial console */
    /* Implementation depends on printf/serial driver availability */
}

void neo_bootscreen_finish(void) {
    /* Minimal finish */
}
