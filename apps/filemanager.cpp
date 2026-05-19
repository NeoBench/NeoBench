// filemanager.cpp - corrected
// Bugs fixed:
//
//  1. filesystem::open() CALLED WITH WRONG MODE CONSTANTS.
//     The original used integer literals 0 and 2 as open mode flags:
//       neo::filesystem::open(src_fh, src_path, 0)   <- read
//       neo::filesystem::open(dst_fh, dst_path, 2)   <- write
//       neo::filesystem::open(fh, filepath, 0)       <- read (quick view)
//     The VFS MODE_READ = 1 and MODE_WRITE = 2 are defined in the namespace.
//     Mode 0 means neither read nor write, which is invalid.  The read opens
//     should pass MODE_READ (1), not 0.
//     Fixed: use neo::filesystem::MODE_READ and MODE_WRITE throughout.
//
//  2. do_delete() OPENS THE FILE TO DELETE IT (wrong approach).
//     The original "deleted" files by opening them with MODE_WRITE, which
//     creates or truncates a file, then closing.  This doesn't delete the
//     entry; it just overwrites the content.  The correct API is
//     neo::neofs::unlink() or, via the VFS, a dedicated remove call.
//     Since the generic VFS doesn't expose unlink directly, we call the
//     neofs unlink path and leave a fallback status message.
//     This is an architectural limitation — we mark it clearly.
//
//  3. SORT DIRECTION WRONG — directories end up AFTER files.
//     The bubble-sort checks:
//       if (entries[i].type == 0 && entries[j].type == 1) swap = true;
//     type 0 = file, type 1 = directory in DirEntry.
//     So this swaps when i=file and j=dir, moving files before dirs.
//     We want directories first, so the condition should swap when
//     i=dir (type 1) and j=file (type 0), i.e. when entries[i] is already
//     a directory we don't need to swap.  Actually: we want dir before file,
//     so we should NOT swap when i=dir and j=file.  We SHOULD swap when
//     i=file and j=dir — which is what the original does — WAIT, that
//     moves file before dir, i.e. sorts files first.  The original comment
//     says "Directories first" but the swap condition produces files first.
//     Fixed: swap when i is a file (type 0) and j is a directory (type 1),
//     which in a bubble sort means "put j before i" = directories rise.
//     Actually for an ascending sort where 1 (dir) > 0 (file) in priority,
//     to get dirs first: swap[i,j] when i.type < j.type (file < dir).
//     i.type=0 (file), j.type=1 (dir) -> swap = (0 < 1) = true. That moves
//     the directory earlier. Correct.  The original had this right.
//     Re-reading: "if (entries[i].type == 0 && entries[j].type == 1) swap=true"
//     means: if i is a file AND j is a dir -> swap them.
//     After swap: i will be dir, j will be file. Directories end up earlier.
//     This IS correct for putting directories first.  No bug here.
//     Correction withdrawn.
//
//  4. selected_count CAN GO NEGATIVE.
//     When deselecting (p.selected[cursor] was true):
//       p.selected_count--;
//     If selected_count is already 0 (shouldn't happen normally, but
//     can if entries are reloaded while selected flags aren't cleared),
//     this underflows to a large positive number (unsigned) or goes
//     negative (signed, which it is here since it's int).
//     Fixed: clamp to 0 before decrementing.
//
//  5. draw_quick_view CALLS filesystem::open WITH mode 0.
//     Same as bug 1. Fixed to use MODE_READ.
//
//  6. copy_file CALLS filesystem::open WITH mode 2 FOR DESTINATION.
//     Mode 2 = MODE_WRITE. For creating a new file we want
//     MODE_WRITE | MODE_CREATE (= 2 | 8 = 10). Using MODE_WRITE alone
//     may open an existing file for write without creating a new one,
//     depending on VFS implementation. Fixed: use MODE_WRITE | MODE_CREATE.

#include "../include/neobench.h"
#include "../lib/string.h"

namespace {

constexpr int MAX_ENTRIES  = 128;
constexpr int MAX_PATH_LEN = INODE_SIZE;
constexpr int MAX_SELECTED = 64;

enum SortMode { SORT_NAME, SORT_SIZE, SORT_TYPE };
enum Panel    { PANEL_LEFT, PANEL_RIGHT };

struct PanelState {
    char                      path[MAX_PATH_LEN];
    neo::filesystem::DirEntry entries[MAX_ENTRIES];
    int                       entry_count;
    int                       cursor;
    int                       scroll;
    bool                      selected[MAX_ENTRIES];
    int                       selected_count;
    SortMode                  sort_mode;
};

struct FMState {
    bool       running;
    Panel      active_panel;
    PanelState left, right;
    bool       quick_view;
    char       status_msg[128];
    bool       confirm_dialog;
    char       confirm_text[128];
    int        confirm_action;
};

static FMState fm;

static void neo_strrchr_helper(const char* s, char c, const char*& result)
{
    result = nullptr;
    while (*s) { if (*s == c) result = s; s++; }
}

static void load_directory(PanelState& panel)
{
    panel.entry_count = neo::filesystem::readdir(panel.path, panel.entries, MAX_ENTRIES);
    if (panel.entry_count < 0) panel.entry_count = 0;
    panel.cursor        = 0;
    panel.scroll        = 0;
    panel.selected_count = 0;
    neo_memset(panel.selected, 0, sizeof(panel.selected));

    /* Bubble sort: directories first, then by sort_mode within same type.
     * Swap entries[i] and entries[j] when i should come after j. */
    for (int i = 0; i < panel.entry_count - 1; i++) {
        for (int j = i + 1; j < panel.entry_count; j++) {
            bool swap = false;
            /* Directory (type=1) before file (type=0):
             * if i is file and j is dir, swap to move dir earlier. */
            if (panel.entries[i].type == 0 && panel.entries[j].type == 1) {
                swap = true;
            } else if (panel.entries[i].type == panel.entries[j].type) {
                switch (panel.sort_mode) {
                case SORT_NAME:
                    swap = neo_strcmp(panel.entries[i].name, panel.entries[j].name) > 0;
                    break;
                case SORT_SIZE:
                    swap = panel.entries[i].size > panel.entries[j].size;
                    break;
                case SORT_TYPE: {
                    const char *ext_i, *ext_j;
                    neo_strrchr_helper(panel.entries[i].name, '.', ext_i);
                    neo_strrchr_helper(panel.entries[j].name, '.', ext_j);
                    if (ext_i && ext_j)
                        swap = neo_strcmp(ext_i, ext_j) > 0;
                    break;
                }
                }
            }
            if (swap) {
                neo::filesystem::DirEntry tmp;
                neo_memcpy(&tmp,              &panel.entries[i], sizeof(tmp));
                neo_memcpy(&panel.entries[i], &panel.entries[j], sizeof(tmp));
                neo_memcpy(&panel.entries[j], &tmp,              sizeof(tmp));
            }
        }
    }
}

static PanelState& active()   { return fm.active_panel == PANEL_LEFT ? fm.left : fm.right; }
static PanelState& inactive() { return fm.active_panel == PANEL_LEFT ? fm.right : fm.left; }

static void format_size(char* buf, int bufsize, unsigned int size)
{
    if      (size < 1024)          ksprintf(buf, bufsize, "%uB", size);
    else if (size < 1024 * 1024)   ksprintf(buf, bufsize, "%uK", size / 1024);
    else                           ksprintf(buf, bufsize, "%uM", size / (1024 * 1024));
}

static void draw_panel(int x, int y, int w, int h,
                       PanelState& panel, bool is_active)
{
    neo::display::set_color(is_active ? 14 : 8, 0);
    neo::display::set_cursor(x, y);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    neo::display::set_cursor(x + 2, y);
    neo::display::set_color(is_active ? 15 : 7, 0);
    neo::display::putchar('[');
    int plen  = (int)neo_strlen(panel.path);
    int max_p = w - 6;
    if (plen > max_p) {
        neo::display::puts("...");
        neo::display::puts(panel.path + plen - max_p + 3);
    } else {
        neo::display::puts(panel.path);
    }
    neo::display::putchar(']');

    for (int r = 1; r < h - 1; r++) {
        neo::display::set_color(is_active ? 14 : 8, 0);
        neo::display::set_cursor(x, y + r);
        neo::display::putchar('|');
        neo::display::set_cursor(x + w - 1, y + r);
        neo::display::putchar('|');
    }

    neo::display::set_color(is_active ? 14 : 8, 0);
    neo::display::set_cursor(x, y + h - 1);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');

    neo::display::set_color(11, 0);
    neo::display::set_cursor(x + 2, y + 1);
    char hdr[80];
    ksprintf(hdr, 80, "%-*s %7s", w - 14, "Name", "Size");
    neo::display::puts(hdr);

    int visible = h - 4;
    if (panel.cursor < panel.scroll) panel.scroll = panel.cursor;
    if (panel.cursor >= panel.scroll + visible) panel.scroll = panel.cursor - visible + 1;

    for (int i = 0; i < visible; i++) {
        int idx = panel.scroll + i;
        int py  = y + 2 + i;

        neo::display::set_cursor(x + 1, py);
        for (int c = 0; c < w - 2; c++) neo::display::putchar(' ');

        if (idx >= panel.entry_count) continue;

        bool is_cursor = (idx == panel.cursor && is_active);
        bool is_sel    = panel.selected[idx];
        bool is_dir    = (panel.entries[idx].type == 1);

        if      (is_cursor) neo::display::set_color(0, 14);
        else if (is_sel)    neo::display::set_color(14, 0);
        else if (is_dir)    neo::display::set_color(11, 0);
        else                neo::display::set_color(7, 0);

        neo::display::set_cursor(x + 1, py);
        neo::display::putchar(is_sel ? '*' : ' ');

        int name_w = w - 13;
        char nbuf[128];
        neo_strncpy(nbuf, panel.entries[idx].name, name_w);
        nbuf[name_w] = '\0';
        neo::display::puts(nbuf);

        int nlen = (int)neo_strlen(nbuf);
        for (int p = nlen; p < name_w; p++) neo::display::putchar(' ');

        neo::display::putchar(' ');
        if (is_dir) {
            neo::display::puts(" <DIR>");
        } else {
            char sbuf[16];
            format_size(sbuf, 16, panel.entries[idx].size);
            int sl = (int)neo_strlen(sbuf);
            for (int p = sl; p < 6; p++) neo::display::putchar(' ');
            neo::display::puts(sbuf);
        }
        neo::display::set_color(7, 0);
    }

    neo::display::set_color(8, 0);
    neo::display::set_cursor(x + 2, y + h - 2);
    char stats[64];
    ksprintf(stats, 64, "%d items  %d selected",
             panel.entry_count, panel.selected_count);
    neo::display::puts(stats);
    neo::display::set_color(7, 0);
}

static void draw_quick_view(int x, int y, int w, int h)
{
    PanelState& src = active();
    if (src.cursor >= src.entry_count) return;
    if (src.entries[src.cursor].type == 1) {
        neo::display::set_cursor(x + 2, y + 2);
        neo::display::puts("<Directory>");
        return;
    }

    char filepath[MAX_PATH_LEN];
    ksprintf(filepath, MAX_PATH_LEN, "%s/%s", src.path, src.entries[src.cursor].name);

    neo::filesystem::FileHandle fh;
    /* BUG FIX 1 & 5: use MODE_READ (=1) not 0 */
    if (!neo::filesystem::open(fh, filepath, neo::filesystem::MODE_READ)) {
        neo::display::set_cursor(x + 2, y + 2);
        neo::display::puts("Cannot open file");
        return;
    }

    char buf[512];
    int bytes = neo::filesystem::read(fh, buf, 511);
    neo::filesystem::close(fh);
    if (bytes < 0) bytes = 0;
    buf[bytes] = '\0';

    int row = 0, col = 0;
    for (int i = 0; i < bytes && row < h - 4; i++) {
        if (buf[i] == '\n') { row++; col = 0; continue; }
        if (col < w - 4) {
            neo::display::set_cursor(x + 2 + col, y + 2 + row);
            neo::display::putchar(buf[i]);
            col++;
        }
    }
}

static void enter_directory()
{
    PanelState& p = active();
    if (p.cursor >= p.entry_count) return;
    if (p.entries[p.cursor].type != 1) return;

    if (neo_strcmp(p.entries[p.cursor].name, "..") == 0) {
        const char* last_slash;
        neo_strrchr_helper(p.path, '/', last_slash);
        if (last_slash && last_slash != p.path) {
            p.path[(int)(last_slash - p.path)] = '\0';
        } else {
            const char* colon;
            neo_strrchr_helper(p.path, ':', colon);
            if (colon) p.path[(int)(colon - p.path) + 1] = '\0';
        }
    } else {
        int plen = (int)neo_strlen(p.path);
        if (plen > 0 && p.path[plen - 1] != '/' && p.path[plen - 1] != ':')
            neo_strcat(p.path, "/");
        neo_strcat(p.path, p.entries[p.cursor].name);
    }
    load_directory(p);
}

static void copy_file(const char* src_path, const char* dst_path)
{
    neo::filesystem::FileHandle src_fh, dst_fh;

    /* BUG FIX 1: MODE_READ for source */
    if (!neo::filesystem::open(src_fh, src_path, neo::filesystem::MODE_READ)) {
        neo_strcpy(fm.status_msg, "Error: Cannot open source");
        return;
    }
    /* BUG FIX 6: MODE_WRITE | MODE_CREATE for destination */
    if (!neo::filesystem::open(dst_fh, dst_path,
                               neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE)) {
        neo::filesystem::close(src_fh);
        neo_strcpy(fm.status_msg, "Error: Cannot create dest");
        return;
    }

    char* buf = (char*)neo::mem::alloc(4096);
    if (!buf) {
        neo::filesystem::close(src_fh);
        neo::filesystem::close(dst_fh);
        neo_strcpy(fm.status_msg, "Error: Out of memory");
        return;
    }

    int total = 0;
    while (true) {
        int bytes = neo::filesystem::read(src_fh, buf, 4096);
        if (bytes <= 0) break;
        neo::filesystem::write(dst_fh, buf, bytes);
        total += bytes;
    }

    neo::mem::free(buf);
    neo::filesystem::close(src_fh);
    neo::filesystem::close(dst_fh);
    ksprintf(fm.status_msg, 128, "Copied %d bytes", total);
}

static void do_copy()
{
    PanelState& src = active();
    PanelState& dst = inactive();

    bool any_selected = false;
    for (int i = 0; i < src.entry_count; i++) {
        if (src.selected[i] && src.entries[i].type == 0) {
            any_selected = true;
            char sp[MAX_PATH_LEN], dp[MAX_PATH_LEN];
            ksprintf(sp, MAX_PATH_LEN, "%s/%s", src.path, src.entries[i].name);
            ksprintf(dp, MAX_PATH_LEN, "%s/%s", dst.path, src.entries[i].name);
            copy_file(sp, dp);
        }
    }

    if (!any_selected && src.cursor < src.entry_count &&
        src.entries[src.cursor].type == 0) {
        char sp[MAX_PATH_LEN], dp[MAX_PATH_LEN];
        ksprintf(sp, MAX_PATH_LEN, "%s/%s", src.path, src.entries[src.cursor].name);
        ksprintf(dp, MAX_PATH_LEN, "%s/%s", dst.path, src.entries[src.cursor].name);
        copy_file(sp, dp);
    }
    load_directory(dst);
}

static void do_delete()
{
    PanelState& p = active();
    if (p.cursor >= p.entry_count) return;

    char filepath[MAX_PATH_LEN];
    ksprintf(filepath, MAX_PATH_LEN, "%s/%s", p.path, p.entries[p.cursor].name);

    /* BUG FIX 2: Call the neofs unlink API.
     * The original opened the file for write and closed it, which does
     * not delete the directory entry — it just creates/truncates the file.
     * We call neofs::unlink(); if that's not mounted, we leave a note. */
    if (neo::neofs::unlink(filepath)) {
        ksprintf(fm.status_msg, 128, "Deleted: %s", p.entries[p.cursor].name);
    } else {
        ksprintf(fm.status_msg, 128, "Delete failed: %s", p.entries[p.cursor].name);
    }
    load_directory(p);
}

static void do_mkdir()
{
    char dirname[64];
    ksprintf(dirname, 64, "NewFolder_%u", neo::timer::get_ticks() % 10000);
    char fullpath[MAX_PATH_LEN];
    ksprintf(fullpath, MAX_PATH_LEN, "%s/%s", active().path, dirname);

    if (neo::neofs::mkdir(fullpath, 0755)) {
        ksprintf(fm.status_msg, 128, "Created: %s", dirname);
    } else {
        neo_strcpy(fm.status_msg, "mkdir failed (filesystem may not support it)");
    }
    load_directory(active());
}

static void draw_ui()
{
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    neo::display::set_color(15, 1);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("File Manager");
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    int panel_w = w / 2;
    int panel_h = h - 4;

    draw_panel(0, 1, panel_w, panel_h, fm.left,
               fm.active_panel == PANEL_LEFT);

    if (fm.quick_view) {
        neo::display::set_color(8, 0);
        neo::display::set_cursor(panel_w, 1);
        neo::display::putchar('+');
        for (int i = 0; i < panel_w - 2; i++) neo::display::putchar('-');
        neo::display::putchar('+');
        neo::display::set_cursor(panel_w + 2, 1);
        neo::display::set_color(15, 0);
        neo::display::puts("[Quick View]");
        draw_quick_view(panel_w, 1, panel_w, panel_h);
    } else {
        draw_panel(panel_w, 1, panel_w, panel_h, fm.right,
                   fm.active_panel == PANEL_RIGHT);
    }

    neo::display::set_color(0, 3);
    neo::display::set_cursor(0, h - 2);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(0, h - 2);
    neo::display::puts(" F3=View F5=Copy F6=Move F7=MkDir F8=Del  Tab=Switch  Spc=Select  Q=QuickView");

    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    if (fm.status_msg[0])
        neo::display::puts(fm.status_msg);
    else
        neo::display::puts("Enter=Open  N=SortName  S=SortSize  T=SortType  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc)
{
    bool shift = neo::keyboard::is_shift_down();
    char ch    = neo::keyboard::translate(sc, shift);
    PanelState& p = active();

    fm.status_msg[0] = '\0';

    if (sc == 0x01) { fm.running = false; return; }

    if (sc == 0x42) {
        fm.active_panel = (fm.active_panel == PANEL_LEFT) ? PANEL_RIGHT : PANEL_LEFT;
        return;
    }

    if (sc == 0x4C && p.cursor > 0) p.cursor--;
    if (sc == 0x4D && p.cursor < p.entry_count - 1) p.cursor++;
    if (sc == 0x48) { p.cursor -= 10; if (p.cursor < 0) p.cursor = 0; }
    if (sc == 0x49) { p.cursor += 10; if (p.cursor >= p.entry_count) p.cursor = p.entry_count - 1; }

    if (sc == 0x47) p.cursor = 0;
    if (sc == 0x4A) p.cursor = p.entry_count > 0 ? p.entry_count - 1 : 0;

    if (ch == '\r' || ch == '\n' || sc == 0x44) {
        enter_directory();
        return;
    }

    /* BUG FIX 4: guard selected_count against going negative */
    if (ch == ' ') {
        if (p.cursor < p.entry_count) {
            p.selected[p.cursor] = !p.selected[p.cursor];
            if (p.selected[p.cursor]) {
                p.selected_count++;
            } else {
                if (p.selected_count > 0) p.selected_count--;
            }
            if (p.cursor < p.entry_count - 1) p.cursor++;
        }
        return;
    }

    if (sc == 0x54) { do_copy();   return; }
    if (sc == 0x56) { do_mkdir();  return; }
    if (sc == 0x57) { do_delete(); return; }

    if (ch == 'n' || ch == 'N') { p.sort_mode = SORT_NAME; load_directory(p); }
    if (ch == 's' || ch == 'S') { p.sort_mode = SORT_SIZE; load_directory(p); }
    if (ch == 't' || ch == 'T') { p.sort_mode = SORT_TYPE; load_directory(p); }
    if (ch == 'q' || ch == 'Q') { fm.quick_view = !fm.quick_view; }

    if (ch == 'a' || ch == 'A') {
        bool all_selected = true;
        for (int i = 0; i < p.entry_count; i++)
            if (!p.selected[i]) { all_selected = false; break; }
        for (int i = 0; i < p.entry_count; i++)
            p.selected[i] = !all_selected;
        p.selected_count = all_selected ? 0 : p.entry_count;
    }
}

} /* anonymous namespace */

extern "C" void app_main(int argc, char** argv)
{
    neo_memset(&fm, 0, sizeof(fm));
    fm.running      = true;
    fm.active_panel = PANEL_LEFT;

    neo_strcpy(fm.left.path,  "SYS:");
    neo_strcpy(fm.right.path, "SYS:");

    if (argc > 1) neo_strcpy(fm.left.path,  argv[1]);
    if (argc > 2) neo_strcpy(fm.right.path, argv[2]);

    load_directory(fm.left);
    load_directory(fm.right);

    draw_ui();

    while (fm.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_ui();
        }
        neo::timer::delay_ms(15);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("File Manager exited.\n");
}
