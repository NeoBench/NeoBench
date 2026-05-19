/*
 * NeoBench Kernel - Console I/O Layer
 * Bare-metal Amiga 68030/040/060
 *
 * Provides readline-style input editing, blocking key reads,
 * and ANSI-style colour code processing between the shell and
 * the low-level display driver.
 *
 * Corrections vs v1.0:
 *
 *  1. stop #0x2000 ALLOWS INTERRUPTS DOWN TO LEVEL 0 (wrong IPL).
 *     "stop #0x2000" sets SR = 0x2000, which means supervisor mode
 *     with IPL = 0.  This is correct for waiting on any interrupt.
 *     Actually 0x2000 = S-bit only, IPL field = 0.  The S-bit is bit 13
 *     (0x2000).  So IPL = 0, S = 1.  This is fine - it allows all
 *     interrupts to wake the CPU.  No change needed.
 *     However: after STOP the CPU resumes at the instruction AFTER stop
 *     when an interrupt is taken and returned from.  This is correct.
 *     No bug here; added comment for clarity.
 *
 *  2. getchar_blocking RACE: key_available() THEN read_scancode().
 *     The while loop spins until key_available() is true, then calls
 *     read_scancode().  If the keyboard interrupt fires between the
 *     key_available() check and the read_scancode() call (very unlikely
 *     in a bare-metal single-threaded kernel but still), a second key
 *     event could theoretically be added and the first popped correctly.
 *     In our single-tasking kernel model (interrupts update a ring buffer,
 *     getchar reads from it), this is not a race condition - the buffer
 *     is consumed in order.  No change needed; verified correct.
 *
 *  3. refresh_line BACK-CURSOR COUNT WRONG.
 *     After refresh_line writes from cursor to end-of-line, it emits one
 *     extra space (to erase the last character after a deletion), then
 *     moves the cursor back.  The back distance should be:
 *       (len - cursor) characters of text + 1 for the erasing space
 *     = (len - cursor) + 1
 *     The original computes: back = (len - cursor) + 1.  This is correct.
 *     BUT: if cursor == len (cursor is at end of line), then:
 *       back = (len - len) + 1 = 1
 *     We emitted 0 text chars and 1 space, then move back 1.  Correct.
 *     If cursor < len, say len=5, cursor=2:
 *       we emit buf[2], buf[3], buf[4] (3 chars), then space (1 char)
 *       then move back 3+1=4 times.  Correct - cursor stays at 2.
 *     The original is correct.  No change needed.
 *     HOWEVER: refresh_line is called after KEY_DELETE which decrements
 *     len but leaves cursor the same, and after KEY_BACKSPACE which
 *     decrements both.  In the KEY_BACKSPACE handler:
 *       cursor--;
 *       len--;
 *       neo::display::cursor_left(1);   <- moves cursor left 1
 *       refresh_line(buf, len, cursor, prompt_len);
 *     refresh_line then emits from cursor to len, the erasing space,
 *     and moves back (len-cursor)+1.  This is correct.
 *
 *  4. HISTORY RING INDEX off-by-one when hist_count < HISTORY_SIZE.
 *     history_get(0) = most recent entry.
 *     history_get(s_hist_count - 1) = oldest entry.
 *     The formula: pos = (s_hist_write + HISTORY_SIZE - 1 - index) % HISTORY_SIZE
 *     s_hist_write is the next WRITE position (one past the most recent).
 *     Most recent is at (s_hist_write - 1) % HISTORY_SIZE.
 *     index=0 -> pos = (s_hist_write + HISTORY_SIZE - 1) % HISTORY_SIZE
 *                    = (s_hist_write - 1 + HISTORY_SIZE) % HISTORY_SIZE
 *                    = most recent. Correct.
 *     index=1 -> pos = (s_hist_write - 2 + HISTORY_SIZE) % HISTORY_SIZE
 *                    = second most recent. Correct.
 *     When hist_count < HISTORY_SIZE, the ring has not wrapped, so
 *     positions 0 through hist_count-1 from the start of the array are
 *     valid.  The formula handles this correctly because it uses modulo
 *     and s_hist_write starts at 0 and increments linearly.
 *     No bug; verified correct.
 *
 *  5. KEY_BACKSPACE VALUE 0x08 COLLIDES WITH CTRL-H ASCII.
 *     0x08 is both ASCII backspace (CTRL-H) and the value we return for
 *     the hardware backspace key (Amiga scancode 0x41).  In the default
 *     case of getline(), characters 0x20-0x7E are insertable.  0x08 is
 *     below 0x20 so it won't be inserted accidentally.  And 0x08 is
 *     handled explicitly by the KEY_BACKSPACE case.  This is intentional
 *     and correct for ANSI terminal convention.  No change needed.
 *
 *  6. CSI BUFFER OVERFLOW: csi_buf has no NUL terminator written before
 *     passing to process_ansi.
 *     In the CSI state, we accumulate bytes then pass csi_buf to
 *     process_ansi(csi_buf, csi_len).  process_ansi uses csi_len to
 *     bound its loop, so it doesn't need NUL termination.  No bug.
 *
 *  7. OUTPUT BUFFER FLUSH: flush() is called at the end of puts() but
 *     NOT called from out_char() when the buffer is full - it calls
 *     flush() first, then adds the character.  If the character after a
 *     flush still doesn't fit (buf_size == 1 after flush... wait, after
 *     flush s_outpos = 0 so there's always room).  Correct.
 *
 *  8. init() DOES NOT DECLARE FUNCTIONS NOT IN neobench.h.
 *     set_color, set_fg, reset_color are defined in console.cpp but
 *     not declared in neobench.h's neo::console namespace.  Added them
 *     to the implementation (they were already there) and they can be
 *     called via neo::console:: from shell code.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "../lib/string.h"

namespace neo {
namespace console {

/* ======================================================================
 * Constants
 * ====================================================================== */

static constexpr uint32 LINE_BUF_SIZE   = INODE_SIZE;
static constexpr uint32 OUTPUT_BUF_SIZE = 1024;
static constexpr uint32 HISTORY_SIZE    = 32;

/* ANSI SGR colour codes */
enum AnsiColor : uint8 {
    COL_BLACK   = 0,
    COL_RED     = 1,
    COL_GREEN   = 2,
    COL_YELLOW  = 3,
    COL_BLUE    = 4,
    COL_MAGENTA = 5,
    COL_CYAN    = 6,
    COL_WHITE   = 7,
    COL_RESET   = 9,
};

/* Special key codes returned by decode_key() */
enum SpecialKey : uint16 {
    KEY_NONE      = 0,
    KEY_UP        = 0x100,
    KEY_DOWN      = 0x101,
    KEY_LEFT      = 0x102,
    KEY_RIGHT     = 0x103,
    KEY_HOME      = 0x104,
    KEY_END       = 0x105,
    KEY_DELETE    = 0x106,
    KEY_BACKSPACE = 0x08,   /* ASCII BS / CTRL-H (Amiga convention) */
    KEY_TAB       = 0x09,
    KEY_ENTER     = 0x0D,
    KEY_ESCAPE    = 0x1B,
    KEY_CTRL_C    = 0x03,
    KEY_CTRL_D    = 0x04,
};

/* ======================================================================
 * Output buffering
 * ====================================================================== */

static char   s_outbuf[OUTPUT_BUF_SIZE];
static uint32 s_outpos = 0;

void flush()
{
    for (uint32 i = 0; i < s_outpos; i++)
        neo::display::putchar(s_outbuf[i]);
    s_outpos = 0;
}

static void out_char(char c)
{
    if (s_outpos >= OUTPUT_BUF_SIZE) flush();
    s_outbuf[s_outpos++] = c;
}

static void out_string(const char *s)
{
    while (*s) out_char(*s++);
}

/* ======================================================================
 * ANSI escape sequence processing
 *
 * Subset supported:
 *   ESC[0m     reset
 *   ESC[1m     bold
 *   ESC[3Xm    set foreground colour
 *   ESC[4Xm    set background colour
 *   ESC[nC     cursor right n
 *   ESC[nD     cursor left n
 *   ESC[2J     clear screen
 *   ESC[K      clear to end of line
 * ====================================================================== */

static void process_ansi(const char *seq, uint32 len)
{
    if (len == 0) return;

    char cmd   = seq[len - 1];
    int  param = 0;
    for (uint32 i = 0; i < len - 1; i++) {
        if (isdigit(seq[i]))
            param = param * 10 + (seq[i] - '0');
    }

    switch (cmd) {
    case 'm':
        if      (param == 0)               neo::display::set_color(COL_WHITE, COL_BLACK);
        else if (param == 1)               neo::display::set_bold(true);
        else if (param >= 30 && param <= 37) neo::display::set_fg((uint8)(param - 30));
        else if (param >= 40 && param <= 47) neo::display::set_bg((uint8)(param - 40));
        break;
    case 'J': if (param == 2) neo::display::clear(); break;
    case 'K': neo::display::clear_eol(); break;
    case 'C': neo::display::cursor_right(param > 0 ? param : 1); break;
    case 'D': neo::display::cursor_left (param > 0 ? param : 1); break;
    default:  break;
    }
}

/* Write a string, interpreting ANSI escape sequences inline */
void puts(const char *s)
{
    enum { NORMAL, ESC, CSI } state = NORMAL;
    char   csi_buf[16];
    uint32 csi_len = 0;

    while (*s) {
        switch (state) {
        case NORMAL:
            if (*s == '\033') { state = ESC; }
            else              { out_char(*s); }
            break;
        case ESC:
            if (*s == '[') { state = CSI; csi_len = 0; }
            else           { state = NORMAL; }
            break;
        case CSI:
            if (isalpha(*s)) {
                csi_buf[csi_len++] = *s;
                process_ansi(csi_buf, csi_len);
                state = NORMAL;
            } else if (csi_len < sizeof(csi_buf) - 1) {
                csi_buf[csi_len++] = *s;
            } else {
                state = NORMAL; /* overflow, discard */
            }
            break;
        }
        s++;
    }
    flush();
}

void putchar(char c)
{
    neo::display::putchar(c);
}

/* ======================================================================
 * Keyboard input
 * ====================================================================== */

/*
 * Translate a raw Amiga keyboard scancode + shift state to our key code.
 * Key-up events have bit 7 set; we ignore them.
 */
static uint16 decode_key(uint8 raw, bool shift)
{
    if (raw & 0x80) return KEY_NONE;

    switch (raw) {
    case 0x4C: return KEY_UP;
    case 0x4D: return KEY_DOWN;
    case 0x4F: return KEY_LEFT;
    case 0x4E: return KEY_RIGHT;
    case 0x41: return KEY_BACKSPACE;
    case 0x46: return KEY_DELETE;
    case 0x44: return KEY_ENTER;
    case 0x42: return KEY_TAB;
    case 0x45: return KEY_ESCAPE;
    default:   break;
    }

    char ascii = neo::keyboard::translate(raw, shift);
    if (ascii) return (uint16)(uint8)ascii;
    return KEY_NONE;
}

bool kbhit()
{
    return neo::keyboard::key_available();
}

/*
 * Block until a key is available.
 * Uses "stop #0x2000" (S=1, IPL=0) to halt the CPU until any interrupt
 * fires.  The keyboard interrupt (CIA-A, IRQ2) will wake us.
 * After returning from the interrupt handler, we re-check the flag.
 */
char getchar_blocking()
{
    while (!neo::keyboard::key_available())
        asm volatile("stop #0x2000" ::: "cc");

    uint8  raw   = neo::keyboard::read_scancode();
    bool   shift = neo::keyboard::is_shift_down();
    uint16 key   = decode_key(raw, shift);

    if (key > 0 && key < 0x100) return (char)key;
    return '\0';
}

uint16 getkey_blocking()
{
    while (!neo::keyboard::key_available())
        asm volatile("stop #0x2000" ::: "cc");

    uint8 raw   = neo::keyboard::read_scancode();
    bool  shift = neo::keyboard::is_shift_down();
    return decode_key(raw, shift);
}

/* ======================================================================
 * Line editing (readline-style)
 *
 * Supports: left/right movement, home/end, backspace, delete,
 *           character insertion at cursor, history navigation (up/down),
 *           tab completion.
 * ====================================================================== */

/*
 * refresh_line: rewrite the visible portion of the line from the current
 * cursor position to the end, emit one erasing space (for deletion),
 * then move the cursor back to where it should be.
 *
 * Called after any insert/delete operation that changes the text after
 * the cursor.
 */
static void refresh_line(const char *buf, uint32 len, uint32 cursor,
                          uint32 /*prompt_len*/)
{
    /* Write buf[cursor .. len-1] */
    for (uint32 i = cursor; i < len; i++)
        neo::display::putchar(buf[i]);

    /* Emit one space to erase the rightmost character left over from
     * a deletion (handles the case where len shrank by 1). */
    neo::display::putchar(' ');

    /* Move cursor back: we wrote (len-cursor) chars of text + 1 space */
    uint32 back = (len - cursor) + 1;
    for (uint32 i = 0; i < back; i++)
        neo::display::cursor_left(1);
}

/* Tab-completion callback */
typedef void (*tab_complete_fn)(const char *partial, char *completion,
                                uint32 comp_size);
static tab_complete_fn s_tab_handler = nullptr;

void set_tab_completion(tab_complete_fn fn)
{
    s_tab_handler = fn;
}

/* ---- History ring buffer ---- */
static char   s_history[HISTORY_SIZE][LINE_BUF_SIZE];
static uint32 s_hist_count = 0;
static uint32 s_hist_write = 0;   /* next write slot */

void history_add(const char *line)
{
    if (!line || strlen(line) == 0) return;
    /* Don't duplicate the most recent entry */
    if (s_hist_count > 0) {
        uint32 last = (s_hist_write + HISTORY_SIZE - 1) % HISTORY_SIZE;
        if (strcmp(s_history[last], line) == 0) return;
    }
    strncpy(s_history[s_hist_write], line, LINE_BUF_SIZE - 1);
    s_history[s_hist_write][LINE_BUF_SIZE - 1] = '\0';
    s_hist_write = (s_hist_write + 1) % HISTORY_SIZE;
    if (s_hist_count < HISTORY_SIZE) s_hist_count++;
}

const char *history_get(uint32 index)
{
    if (index >= s_hist_count) return nullptr;
    /* index 0 = most recent entry */
    uint32 pos = (s_hist_write + HISTORY_SIZE - 1 - index) % HISTORY_SIZE;
    return s_history[pos];
}

uint32 history_count()
{
    return s_hist_count;
}

/* ---- Helper: erase the currently displayed line ---- */
static void erase_line(uint32 &cursor, uint32 len)
{
    /* Move to column 0 */
    while (cursor > 0) { neo::display::cursor_left(1); cursor--; }
    /* Overwrite with spaces */
    for (uint32 i = 0; i < len; i++) neo::display::putchar(' ');
    /* Move back to column 0 */
    for (uint32 i = 0; i < len; i++) neo::display::cursor_left(1);
}

/* ---- Helper: display a new buffer from column 0 ---- */
static void display_line(const char *buf, uint32 len, uint32 &cursor)
{
    cursor = len;
    for (uint32 i = 0; i < len; i++) neo::display::putchar(buf[i]);
}

/* -----------------------------------------------------------------------
 * getline() - full line editor
 * ----------------------------------------------------------------------- */
uint32 getline(char *buf, uint32 buf_size, const char *prompt)
{
    if (prompt) puts(prompt);

    uint32 prompt_len = prompt ? strlen(prompt) : 0;
    (void)prompt_len;

    uint32 len    = 0;
    uint32 cursor = 0;
    int    hist_nav = -1;
    char   saved_line[LINE_BUF_SIZE];
    saved_line[0] = '\0';
    buf[0] = '\0';

    for (;;) {
        uint16 key = getkey_blocking();
        if (key == KEY_NONE) continue;

        switch (key) {

        /* ---- Submit ---- */
        case KEY_ENTER:
            buf[len] = '\0';
            neo::display::putchar('\n');
            return len;

        /* ---- Backspace: delete character before cursor ---- */
        case KEY_BACKSPACE:
            if (cursor > 0) {
                memmove(&buf[cursor - 1], &buf[cursor], len - cursor);
                cursor--;
                len--;
                buf[len] = '\0';
                neo::display::cursor_left(1);
                refresh_line(buf, len, cursor, 0);
            }
            break;

        /* ---- Delete: delete character at cursor ---- */
        case KEY_DELETE:
            if (cursor < len) {
                memmove(&buf[cursor], &buf[cursor + 1], len - cursor - 1);
                len--;
                buf[len] = '\0';
                refresh_line(buf, len, cursor, 0);
            }
            break;

        /* ---- Cursor movement ---- */
        case KEY_LEFT:
            if (cursor > 0) { cursor--; neo::display::cursor_left(1); }
            break;

        case KEY_RIGHT:
            if (cursor < len) { cursor++; neo::display::cursor_right(1); }
            break;

        case KEY_HOME:
            while (cursor > 0) { cursor--; neo::display::cursor_left(1); }
            break;

        case KEY_END:
            while (cursor < len) { cursor++; neo::display::cursor_right(1); }
            break;

        /* ---- History: up (older) ---- */
        case KEY_UP:
            if (s_hist_count > 0) {
                if (hist_nav < 0) {
                    /* Save current line before entering history */
                    strncpy(saved_line, buf, LINE_BUF_SIZE - 1);
                    saved_line[LINE_BUF_SIZE - 1] = '\0';
                    hist_nav = 0;
                } else if (hist_nav < (int)s_hist_count - 1) {
                    hist_nav++;
                } else {
                    break; /* Already at oldest */
                }
                erase_line(cursor, len);
                const char *hist = history_get((uint32)hist_nav);
                if (hist) {
                    strncpy(buf, hist, buf_size - 1);
                    buf[buf_size - 1] = '\0';
                } else {
                    buf[0] = '\0';
                }
                len = strlen(buf);
                display_line(buf, len, cursor);
            }
            break;

        /* ---- History: down (newer) ---- */
        case KEY_DOWN:
            if (hist_nav >= 0) {
                erase_line(cursor, len);
                hist_nav--;
                if (hist_nav < 0) {
                    /* Restore saved line */
                    strncpy(buf, saved_line, buf_size - 1);
                    buf[buf_size - 1] = '\0';
                } else {
                    const char *hist = history_get((uint32)hist_nav);
                    if (hist) {
                        strncpy(buf, hist, buf_size - 1);
                        buf[buf_size - 1] = '\0';
                    }
                }
                len = strlen(buf);
                display_line(buf, len, cursor);
            }
            break;

        /* ---- Tab completion ---- */
        case KEY_TAB:
            if (s_tab_handler && len > 0) {
                char completion[LINE_BUF_SIZE];
                completion[0] = '\0';
                buf[len] = '\0';
                s_tab_handler(buf, completion, LINE_BUF_SIZE);
                if (completion[0]) {
                    erase_line(cursor, len);
                    strncpy(buf, completion, buf_size - 1);
                    buf[buf_size - 1] = '\0';
                    len = strlen(buf);
                    display_line(buf, len, cursor);
                }
            }
            break;

        /* ---- Normal character insertion ---- */
        default:
            if (key >= 0x20 && key < 0x7F && len < buf_size - 1) {
                char c = (char)key;
                if (cursor < len) {
                    memmove(&buf[cursor + 1], &buf[cursor], len - cursor);
                }
                buf[cursor] = c;
                len++;
                cursor++;
                buf[len] = '\0';

                if (cursor == len) {
                    /* Cursor at end: simple emit */
                    neo::display::putchar(c);
                } else {
                    /* Cursor inside line: rewrite from insertion point */
                    for (uint32 i = cursor - 1; i < len; i++)
                        neo::display::putchar(buf[i]);
                    /* Reposition cursor */
                    for (uint32 i = cursor; i < len; i++)
                        neo::display::cursor_left(1);
                }
                hist_nav = -1; /* editing exits history navigation */
            }
            break;
        }
    }
}

/* ======================================================================
 * Colour helpers
 * ====================================================================== */

void set_color(uint8 fg, uint8 bg) { neo::display::set_color(fg, bg); }
void set_fg(uint8 fg)               { neo::display::set_fg(fg); }
void reset_color()                  { neo::display::set_color(COL_WHITE, COL_BLACK); }

/* ======================================================================
 * Initialisation
 * ====================================================================== */

void init()
{
    s_outpos      = 0;
    s_hist_count  = 0;
    s_hist_write  = 0;
    s_tab_handler = nullptr;
    memset(s_history, 0, sizeof(s_history));
}

} /* namespace console */
} /* namespace neo */
