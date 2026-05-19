#include "../include/neobench.h"
#include "../lib/string.h"

// NeoCalendar - Calendar Planner with Events
// Monthly grid, navigate months/years, events, daily/weekly/monthly views,
// search, recurring events, save/load

namespace {

constexpr int MAX_EVENTS = 128;
constexpr int MAX_TITLE = 40;

enum CalView { CAL_MONTH, CAL_WEEK, CAL_DAY, CAL_EVENTLIST };

struct Event {
    int year, month, day;
    int hour, minute;
    char title[MAX_TITLE];
    bool recurring_weekly;
    bool active;
};

struct CalState {
    bool running;
    CalView view;
    int cur_year, cur_month, cur_day;
    int sel_day;  // selected day in month view
    int today_year, today_month, today_day;
    Event events[MAX_EVENTS];
    int event_count;
    bool adding_event;
    char input_buf[MAX_TITLE];
    int input_len;
    int input_field; // 0=title, 1=hour, 2=minute, 3=recurring
    int new_hour, new_minute;
    bool new_recurring;
    char search_buf[MAX_TITLE];
    int search_len;
    bool searching;
    int search_results[MAX_EVENTS];
    int search_count;
};

static CalState cal;

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m) {
    const int dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = dm[m - 1];
    if (m == 2 && is_leap(y)) d++;
    return d;
}

static int day_of_week(int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    int k = y % 100, j = y / 100;
    int dow = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    return ((dow + 6) % 7); // 0=Sun
}

static const char* month_names[] = {"January","February","March","April","May","June",
    "July","August","September","October","November","December"};
static const char* day_abbr[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

static int count_events_on(int y, int m, int d) {
    int dow_target = day_of_week(y, m, d);
    int count = 0;
    for (int i = 0; i < cal.event_count; i++) {
        if (!cal.events[i].active) continue;
        if (cal.events[i].year == y && cal.events[i].month == m && cal.events[i].day == d) {
            count++;
        } else if (cal.events[i].recurring_weekly) {
            int edow = day_of_week(cal.events[i].year, cal.events[i].month, cal.events[i].day);
            if (edow == dow_target) count++;
        }
    }
    return count;
}

static void get_today() {
    if (neo::rtc::is_present()) {
        neo::rtc::DateTime dt;
        neo::rtc::read(dt);
        cal.today_year = dt.year;
        cal.today_month = dt.month;
        cal.today_day = dt.day;
    } else {
        cal.today_year = 2026;
        cal.today_month = 1;
        cal.today_day = 1;
    }
}

static void draw_box(int x, int y, int w, int h) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');
    for (int r = 1; r < h - 1; r++) {
        neo::display::set_cursor(x, y + r);
        neo::display::putchar('|');
        neo::display::set_cursor(x + w - 1, y + r);
        neo::display::putchar('|');
    }
    neo::display::set_cursor(x, y + h - 1);
    neo::display::putchar('+');
    for (int i = 0; i < w - 2; i++) neo::display::putchar('-');
    neo::display::putchar('+');
}

static void draw_month_view() {
    int w = neo::display::get_width();

    // Month/Year header
    char hdr[64];
    ksprintf(hdr, 64, "< %s %d >", month_names[cal.cur_month - 1], cal.cur_year);
    neo::display::set_bold(true);
    neo::display::set_color(14, 0);
    neo::display::set_cursor((w - neo_strlen(hdr)) / 2, 2);
    neo::display::puts(hdr);
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    // Day headers
    int grid_x = (w - 7 * 5) / 2;
    if (grid_x < 1) grid_x = 1;
    neo::display::set_color(11, 0);
    for (int d = 0; d < 7; d++) {
        neo::display::set_cursor(grid_x + d * 5, 4);
        neo::display::puts(day_abbr[d]);
    }
    neo::display::set_color(7, 0);

    // Calendar grid
    int first_dow = day_of_week(cal.cur_year, cal.cur_month, 1);
    int dim = days_in_month(cal.cur_year, cal.cur_month);
    int row = 0, col = first_dow;

    for (int d = 1; d <= dim; d++) {
        int px = grid_x + col * 5;
        int py = 6 + row * 2;

        bool is_today = (d == cal.today_day && cal.cur_month == cal.today_month && cal.cur_year == cal.today_year);
        bool is_sel = (d == cal.sel_day);
        int evts = count_events_on(cal.cur_year, cal.cur_month, d);

        if (is_sel) {
            neo::display::set_color(0, 14);
        } else if (is_today) {
            neo::display::set_color(0, 10);
        } else if (evts > 0) {
            neo::display::set_color(12, 0);
        } else {
            neo::display::set_color(7, 0);
        }

        neo::display::set_cursor(px, py);
        char dbuf[8];
        ksprintf(dbuf, 8, "%2d", d);
        neo::display::puts(dbuf);

        if (evts > 0) {
            neo::display::set_cursor(px + 2, py);
            neo::display::putchar('*');
        }

        neo::display::set_color(7, 0);

        col++;
        if (col >= 7) { col = 0; row++; }
    }

    // Events for selected day
    int ey = 6 + (row + 1) * 2 + 1;
    neo::display::set_bold(true);
    neo::display::set_cursor(2, ey);
    char selbuf[48];
    ksprintf(selbuf, 48, "Events for %s %d, %d:", month_names[cal.cur_month - 1], cal.sel_day, cal.cur_year);
    neo::display::puts(selbuf);
    neo::display::set_bold(false);
    ey++;

    int dow_sel = day_of_week(cal.cur_year, cal.cur_month, cal.sel_day);
    int shown = 0;
    for (int i = 0; i < cal.event_count; i++) {
        if (!cal.events[i].active) continue;
        bool match = (cal.events[i].year == cal.cur_year && cal.events[i].month == cal.cur_month && cal.events[i].day == cal.sel_day);
        if (!match && cal.events[i].recurring_weekly) {
            int edow = day_of_week(cal.events[i].year, cal.events[i].month, cal.events[i].day);
            if (edow == dow_sel) match = true;
        }
        if (match) {
            char ebuf[80];
            ksprintf(ebuf, 80, "  %02d:%02d - %s%s", cal.events[i].hour, cal.events[i].minute,
                     cal.events[i].title, cal.events[i].recurring_weekly ? " (weekly)" : "");
            neo::display::set_cursor(2, ey + shown);
            neo::display::set_color(10, 0);
            neo::display::puts(ebuf);
            neo::display::set_color(7, 0);
            shown++;
            if (shown >= 6) break;
        }
    }
    if (shown == 0) {
        neo::display::set_cursor(4, ey);
        neo::display::set_color(8, 0);
        neo::display::puts("(no events)");
        neo::display::set_color(7, 0);
    }
}

static void draw_day_view() {
    int w = neo::display::get_width();
    char hdr[64];
    ksprintf(hdr, 64, "%s %d, %d - Daily Schedule", month_names[cal.cur_month - 1], cal.sel_day, cal.cur_year);
    neo::display::set_bold(true);
    neo::display::set_cursor((w - neo_strlen(hdr)) / 2, 2);
    neo::display::puts(hdr);
    neo::display::set_bold(false);

    // Show hour slots 6-22
    for (int h = 6; h <= 22; h++) {
        int y = 4 + (h - 6);
        char tbuf[8];
        ksprintf(tbuf, 8, "%02d:00", h);
        neo::display::set_color(8, 0);
        neo::display::set_cursor(2, y);
        neo::display::puts(tbuf);
        neo::display::set_color(7, 0);
        neo::display::puts(" |");

        // Check events at this hour
        int dow_sel = day_of_week(cal.cur_year, cal.cur_month, cal.sel_day);
        for (int i = 0; i < cal.event_count; i++) {
            if (!cal.events[i].active) continue;
            if (cal.events[i].hour != h) continue;
            bool match = (cal.events[i].year == cal.cur_year && cal.events[i].month == cal.cur_month && cal.events[i].day == cal.sel_day);
            if (!match && cal.events[i].recurring_weekly) {
                int edow = day_of_week(cal.events[i].year, cal.events[i].month, cal.events[i].day);
                if (edow == dow_sel) match = true;
            }
            if (match) {
                neo::display::set_color(10, 0);
                neo::display::putchar(' ');
                neo::display::puts(cal.events[i].title);
                neo::display::set_color(7, 0);
            }
        }
    }
}

static void draw_week_view() {
    int w = neo::display::get_width();
    // Find start of week (Sunday)
    int dow = day_of_week(cal.cur_year, cal.cur_month, cal.sel_day);
    int col_w = (w - 8) / 7;

    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    char hdr[64];
    ksprintf(hdr, 64, "Week View - %s %d", month_names[cal.cur_month - 1], cal.cur_year);
    neo::display::puts(hdr);
    neo::display::set_bold(false);

    // Day headers
    for (int d = 0; d < 7; d++) {
        int day_num = cal.sel_day - dow + d;
        int dm = days_in_month(cal.cur_year, cal.cur_month);
        if (day_num < 1) day_num += dm;
        if (day_num > dm) day_num -= dm;

        neo::display::set_cursor(8 + d * col_w, 4);
        neo::display::set_color(11, 0);
        char dbuf[16];
        ksprintf(dbuf, 16, "%s %d", day_abbr[d], day_num);
        neo::display::puts(dbuf);
        neo::display::set_color(7, 0);
    }

    // Grid lines
    for (int h = 8; h <= 18; h++) {
        int y = 6 + (h - 8);
        char tbuf[8];
        ksprintf(tbuf, 8, "%02d:00", h);
        neo::display::set_color(8, 0);
        neo::display::set_cursor(2, y);
        neo::display::puts(tbuf);
        neo::display::set_color(7, 0);
    }
}

static void draw_add_event_dialog() {
    int w = neo::display::get_width();
    int h = neo::display::get_height();
    int dw = 50, dh = 12;
    int dx = (w - dw) / 2, dy = (h - dh) / 2;

    draw_box(dx, dy, dw, dh);
    neo::display::set_bold(true);
    neo::display::set_cursor(dx + 2, dy + 1);
    neo::display::puts("Add Event");
    neo::display::set_bold(false);

    char datebuf[32];
    ksprintf(datebuf, 32, "Date: %d/%d/%d", cal.cur_month, cal.sel_day, cal.cur_year);
    neo::display::set_cursor(dx + 2, dy + 3);
    neo::display::puts(datebuf);

    neo::display::set_cursor(dx + 2, dy + 4);
    neo::display::puts("Title: ");
    if (cal.input_field == 0) neo::display::set_color(14, 0);
    neo::display::puts(cal.input_buf);
    neo::display::putchar('_');
    neo::display::set_color(7, 0);

    char tbuf[32];
    ksprintf(tbuf, 32, "Time:  %02d:%02d", cal.new_hour, cal.new_minute);
    neo::display::set_cursor(dx + 2, dy + 5);
    if (cal.input_field >= 1 && cal.input_field <= 2) neo::display::set_color(14, 0);
    neo::display::puts(tbuf);
    neo::display::set_color(7, 0);

    neo::display::set_cursor(dx + 2, dy + 6);
    neo::display::puts("Weekly: ");
    if (cal.input_field == 3) neo::display::set_color(14, 0);
    neo::display::puts(cal.new_recurring ? "[Yes]" : "[No]");
    neo::display::set_color(7, 0);

    neo::display::set_cursor(dx + 2, dy + 8);
    neo::display::puts("Tab=Next  Enter=Save  Esc=Cancel");
    neo::display::set_cursor(dx + 2, dy + 9);
    neo::display::puts("In Time: Up/Down to change");
}

static void draw_search_view() {
    int w = neo::display::get_width();
    neo::display::set_bold(true);
    neo::display::set_cursor(2, 2);
    neo::display::puts("Search Events");
    neo::display::set_bold(false);

    neo::display::set_cursor(2, 4);
    neo::display::puts("Query: ");
    neo::display::set_color(14, 0);
    neo::display::puts(cal.search_buf);
    neo::display::putchar('_');
    neo::display::set_color(7, 0);

    if (cal.search_count > 0) {
        neo::display::set_cursor(2, 6);
        char rbuf[32];
        ksprintf(rbuf, 32, "Found %d results:", cal.search_count);
        neo::display::puts(rbuf);

        for (int i = 0; i < cal.search_count && i < 15; i++) {
            int idx = cal.search_results[i];
            char line[80];
            ksprintf(line, 80, "  %d/%d/%d %02d:%02d - %s",
                cal.events[idx].month, cal.events[idx].day, cal.events[idx].year,
                cal.events[idx].hour, cal.events[idx].minute, cal.events[idx].title);
            neo::display::set_cursor(2, 8 + i);
            neo::display::puts(line);
        }
    }
}

static void do_search() {
    cal.search_count = 0;
    if (cal.search_len == 0) return;
    for (int i = 0; i < cal.event_count; i++) {
        if (!cal.events[i].active) continue;
        // Simple substring search
        int tlen = neo_strlen(cal.events[i].title);
        int slen = cal.search_len;
        for (int j = 0; j <= tlen - slen; j++) {
            bool match = true;
            for (int k = 0; k < slen; k++) {
                char a = cal.events[i].title[j + k];
                char b = cal.search_buf[k];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                cal.search_results[cal.search_count++] = i;
                break;
            }
        }
    }
}

static void save_events() {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, "SYS:calendar.dat", neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
        // Write count
        neo::filesystem::write(fh, &cal.event_count, sizeof(int));
        for (int i = 0; i < cal.event_count; i++) {
            neo::filesystem::write(fh, &cal.events[i], sizeof(Event));
        }
        neo::filesystem::close(fh);
    }
}

static void load_events() {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, "SYS:calendar.dat", neo::filesystem::MODE_READ) == 0) {
        neo::filesystem::read(fh, &cal.event_count, sizeof(int));
        if (cal.event_count > MAX_EVENTS) cal.event_count = MAX_EVENTS;
        for (int i = 0; i < cal.event_count; i++) {
            neo::filesystem::read(fh, &cal.events[i], sizeof(Event));
        }
        neo::filesystem::close(fh);
    }
}

static void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    // Title bar
    neo::display::set_color(15, 1);
    neo::display::set_cursor(0, 0);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(2, 0);
    neo::display::set_bold(true);
    neo::display::puts("NeoCalendar");
    neo::display::set_bold(false);

    const char* vtabs[] = {"[F1]Month","[F2]Week","[F3]Day","[F4]Events"};
    int tx = 20;
    for (int i = 0; i < 4; i++) {
        neo::display::set_cursor(tx, 0);
        if ((int)cal.view == i) neo::display::set_color(14, 1);
        else neo::display::set_color(7, 1);
        neo::display::puts(vtabs[i]);
        tx += neo_strlen(vtabs[i]) + 2;
    }
    neo::display::set_color(7, 0);

    if (cal.searching) {
        draw_search_view();
    } else if (cal.adding_event) {
        draw_month_view();
        draw_add_event_dialog();
    } else {
        switch (cal.view) {
            case CAL_MONTH: draw_month_view(); break;
            case CAL_WEEK:  draw_week_view(); break;
            case CAL_DAY:   draw_day_view(); break;
            case CAL_EVENTLIST: draw_search_view(); break;
        }
    }

    // Status bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("Arrows=Nav  A=AddEvent  /=Search  S=Save  PgUp/Dn=Month  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(sc, shift);

    if (sc == 0x01) {
        if (cal.adding_event) { cal.adding_event = false; return; }
        if (cal.searching) { cal.searching = false; return; }
        save_events();
        cal.running = false;
        return;
    }

    // Search mode input
    if (cal.searching) {
        if (ch >= 32 && ch < 127 && cal.search_len < MAX_TITLE - 1) {
            cal.search_buf[cal.search_len++] = ch;
            cal.search_buf[cal.search_len] = 0;
            do_search();
        }
        if (sc == 0x41 && cal.search_len > 0) {
            cal.search_buf[--cal.search_len] = 0;
            do_search();
        }
        return;
    }

    // Add event dialog input
    if (cal.adding_event) {
        if (sc == 0x42) { // Tab
            cal.input_field = (cal.input_field + 1) % 4;
            return;
        }
        if (cal.input_field == 0) {
            if (ch >= 32 && ch < 127 && cal.input_len < MAX_TITLE - 1) {
                cal.input_buf[cal.input_len++] = ch;
                cal.input_buf[cal.input_len] = 0;
            }
            if (sc == 0x41 && cal.input_len > 0) {
                cal.input_buf[--cal.input_len] = 0;
            }
        }
        if (cal.input_field == 1) {
            if (sc == 0x4C) cal.new_hour = (cal.new_hour + 1) % 24;
            if (sc == 0x4D) cal.new_hour = cal.new_hour > 0 ? cal.new_hour - 1 : 23;
        }
        if (cal.input_field == 2) {
            if (sc == 0x4C) cal.new_minute = (cal.new_minute + 5) % 60;
            if (sc == 0x4D) cal.new_minute = cal.new_minute >= 5 ? cal.new_minute - 5 : 55;
        }
        if (cal.input_field == 3) {
            if (sc == 0x4C || sc == 0x4D || ch == ' ') cal.new_recurring = !cal.new_recurring;
        }
        // Enter to save
        if (ch == '\r' || ch == '\n' || sc == 0x44) {
            if (cal.input_len > 0 && cal.event_count < MAX_EVENTS) {
                Event& e = cal.events[cal.event_count];
                e.year = cal.cur_year;
                e.month = cal.cur_month;
                e.day = cal.sel_day;
                e.hour = cal.new_hour;
                e.minute = cal.new_minute;
                neo_strcpy(e.title, cal.input_buf);
                e.recurring_weekly = cal.new_recurring;
                e.active = true;
                cal.event_count++;
                cal.adding_event = false;
            }
        }
        return;
    }

    // View switching
    if (sc == 0x50) { cal.view = CAL_MONTH; return; }
    if (sc == 0x51) { cal.view = CAL_WEEK; return; }
    if (sc == 0x52) { cal.view = CAL_DAY; return; }
    if (sc == 0x53) { cal.view = CAL_EVENTLIST; cal.searching = true; return; }

    // Navigation
    int dim = days_in_month(cal.cur_year, cal.cur_month);

    if (sc == 0x4F) { // Left
        cal.sel_day--;
        if (cal.sel_day < 1) {
            cal.cur_month--;
            if (cal.cur_month < 1) { cal.cur_month = 12; cal.cur_year--; }
            cal.sel_day = days_in_month(cal.cur_year, cal.cur_month);
        }
    }
    if (sc == 0x4E) { // Right
        cal.sel_day++;
        if (cal.sel_day > dim) {
            cal.sel_day = 1;
            cal.cur_month++;
            if (cal.cur_month > 12) { cal.cur_month = 1; cal.cur_year++; }
        }
    }
    if (sc == 0x4C) { // Up
        cal.sel_day -= 7;
        if (cal.sel_day < 1) {
            cal.cur_month--;
            if (cal.cur_month < 1) { cal.cur_month = 12; cal.cur_year--; }
            cal.sel_day += days_in_month(cal.cur_year, cal.cur_month);
        }
    }
    if (sc == 0x4D) { // Down
        cal.sel_day += 7;
        if (cal.sel_day > dim) {
            cal.sel_day -= dim;
            cal.cur_month++;
            if (cal.cur_month > 12) { cal.cur_month = 1; cal.cur_year++; }
        }
    }

    // Page Up/Down = prev/next month
    if (sc == 0x48) { // PgUp or similar
        cal.cur_month--;
        if (cal.cur_month < 1) { cal.cur_month = 12; cal.cur_year--; }
        dim = days_in_month(cal.cur_year, cal.cur_month);
        if (cal.sel_day > dim) cal.sel_day = dim;
    }
    if (sc == 0x49) { // PgDn
        cal.cur_month++;
        if (cal.cur_month > 12) { cal.cur_month = 1; cal.cur_year++; }
        dim = days_in_month(cal.cur_year, cal.cur_month);
        if (cal.sel_day > dim) cal.sel_day = dim;
    }

    // Add event
    if (ch == 'a' || ch == 'A') {
        cal.adding_event = true;
        cal.input_len = 0;
        cal.input_buf[0] = 0;
        cal.input_field = 0;
        cal.new_hour = 9;
        cal.new_minute = 0;
        cal.new_recurring = false;
    }

    // Search
    if (ch == '/') {
        cal.searching = true;
        cal.search_len = 0;
        cal.search_buf[0] = 0;
        cal.search_count = 0;
    }

    // Save
    if (ch == 's' || ch == 'S') { save_events(); }

    // Delete event on selected day (D key)
    if (ch == 'd' || ch == 'D') {
        for (int i = cal.event_count - 1; i >= 0; i--) {
            if (cal.events[i].active && cal.events[i].year == cal.cur_year &&
                cal.events[i].month == cal.cur_month && cal.events[i].day == cal.sel_day) {
                cal.events[i].active = false;
                break;
            }
        }
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&cal, 0, sizeof(cal));
    cal.running = true;
    cal.view = CAL_MONTH;

    get_today();
    cal.cur_year = cal.today_year;
    cal.cur_month = cal.today_month;
    cal.cur_day = cal.today_day;
    cal.sel_day = cal.today_day;

    load_events();
    draw_ui();

    while (cal.running) {
        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            handle_key(sc);
            draw_ui();
        }
        neo::timer::delay_ms(20);
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("NeoCalendar exited.\n");
}
