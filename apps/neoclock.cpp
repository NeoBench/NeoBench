#include "../include/neobench.h"
#include "../lib/string.h"

// NeoClock - Desktop Clock / World Time / Stopwatch / Timer
// Large ASCII art clock, timezones, stopwatch with laps, countdown timer

namespace {

constexpr int MAX_LAPS = 20;
constexpr double PI_VAL = 3.14159265358979323846;

enum ClockView { VIEW_CLOCK, VIEW_WORLD, VIEW_STOPWATCH, VIEW_TIMER, VIEW_ANALOG };

struct Timezone {
    const char* name;
    int offset_hours;
    int offset_minutes;
};

static const Timezone timezones[] = {
    {"UTC",  0, 0},
    {"EST", -5, 0},
    {"PST", -8, 0},
    {"CET", +1, 0},
    {"JST", +9, 0},
    {"AEST",+10,0},
    {"IST", +5,30},
    {"GMT",  0, 0},
};
constexpr int NUM_TZ = 8;

struct LapTime {
    unsigned int ticks;
    int minutes;
    int seconds;
    int centiseconds;
};

struct ClockState {
    ClockView view;
    bool running;

    // Stopwatch
    bool sw_running;
    unsigned int sw_start_ticks;
    unsigned int sw_elapsed;
    LapTime laps[MAX_LAPS];
    int lap_count;

    // Timer
    int timer_minutes;
    int timer_seconds;
    bool timer_running;
    unsigned int timer_start_ticks;
    unsigned int timer_total_ms;
    bool timer_alarm;
};

static ClockState state;

// Large digit font (5x7 each digit)
static const char* big_digits[10][7] = {
    { " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " },  // 0
    { "  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### " },  // 1
    { " ### ", "#   #", "    #", "  ## ", " #   ", "#    ", "#####" },  // 2
    { " ### ", "#   #", "    #", "  ## ", "    #", "#   #", " ### " },  // 3
    { "   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # " },  // 4
    { "#####", "#    ", "#### ", "    #", "    #", "#   #", " ### " },  // 5
    { " ### ", "#    ", "#### ", "#   #", "#   #", "#   #", " ### " },  // 6
    { "#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   " },  // 7
    { " ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### " },  // 8
    { " ### ", "#   #", "#   #", " ####", "    #", "    #", " ### " },  // 9
};

static const char* big_colon[7] = {
    "   ", " # ", " # ", "   ", " # ", " # ", "   "
};

static void draw_big_digit(int x, int y, int digit) {
    if (digit < 0 || digit > 9) return;
    for (int row = 0; row < 7; row++) {
        neo::display::set_cursor(x, y + row);
        neo::display::puts(big_digits[digit][row]);
    }
}

static void draw_big_colon(int x, int y) {
    for (int row = 0; row < 7; row++) {
        neo::display::set_cursor(x, y + row);
        neo::display::puts(big_colon[row]);
    }
}

static void draw_big_time(int x, int y, int h, int m, int s) {
    neo::display::set_color(14, 0);
    draw_big_digit(x, y, h / 10);
    draw_big_digit(x + 6, y, h % 10);
    draw_big_colon(x + 12, y);
    draw_big_digit(x + 16, y, m / 10);
    draw_big_digit(x + 22, y, m % 10);
    draw_big_colon(x + 28, y);
    draw_big_digit(x + 32, y, s / 10);
    draw_big_digit(x + 38, y, s % 10);
    neo::display::set_color(7, 0);
}

static void get_rtc_time(int& h, int& m, int& s, int& year, int& month, int& day) {
    if (neo::rtc::is_present()) {
        neo::rtc::DateTime dt;
        neo::rtc::read(dt);
        h = dt.hour; m = dt.minute; s = dt.second;
        year = dt.year; month = dt.month; day = dt.day;
    } else {
        unsigned int up = neo::timer::get_uptime_seconds();
        h = (up / 3600) % 24;
        m = (up / 60) % 60;
        s = up % 60;
        year = 2026; month = 1; day = 1;
    }
}

static void apply_tz_offset(int& h, int& m, int off_h, int off_m) {
    m += off_m;
    if (m >= 60) { m -= 60; h++; }
    if (m < 0) { m += 60; h--; }
    h += off_h;
    if (h >= 24) h -= 24;
    if (h < 0) h += 24;
}

static const char* day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* month_names[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

static int day_of_week(int y, int m, int d) {
    // Zeller's formula (adjusted)
    if (m < 3) { m += 12; y--; }
    int k = y % 100;
    int j = y / 100;
    int dow = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    return ((dow + 6) % 7); // 0=Sun
}

static void draw_clock_view() {
    int h, m, s, year, month, day;
    get_rtc_time(h, m, s, year, month, day);
    int w = neo::display::get_width();

    // Center the big time
    int cx = (w - 44) / 2;
    if (cx < 0) cx = 0;
    draw_big_time(cx, 3, h, m, s);

    // Date
    int dow = day_of_week(year, month, day);
    char datebuf[64];
    ksprintf(datebuf, 64, "%s, %s %d, %d", day_names[dow], month_names[month - 1], day, year);
    neo::display::set_color(11, 0);
    neo::display::set_cursor((w - neo_strlen(datebuf)) / 2, 11);
    neo::display::puts(datebuf);
    neo::display::set_color(7, 0);

    // Uptime
    unsigned int up = neo::timer::get_uptime_seconds();
    char upbuf[64];
    ksprintf(upbuf, 64, "Uptime: %dd %dh %dm %ds", up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);
    neo::display::set_cursor((w - neo_strlen(upbuf)) / 2, 13);
    neo::display::puts(upbuf);
}

static void draw_world_view() {
    int h, m, s, year, month, day;
    get_rtc_time(h, m, s, year, month, day);
    int w = neo::display::get_width();

    neo::display::set_bold(true);
    neo::display::set_cursor((w - 16) / 2, 2);
    neo::display::puts("World Time Zones");
    neo::display::set_bold(false);

    neo::display::set_cursor(5, 4);
    neo::display::set_color(8, 0);
    neo::display::puts("Timezone    Time         Offset");
    neo::display::set_color(7, 0);

    for (int i = 0; i < NUM_TZ; i++) {
        int th = h, tm = m;
        apply_tz_offset(th, tm, timezones[i].offset_hours, timezones[i].offset_minutes);

        char line[80];
        char off_str[16];
        if (timezones[i].offset_minutes != 0) {
            ksprintf(off_str, 16, "%+d:%02d", timezones[i].offset_hours, timezones[i].offset_minutes < 0 ? -timezones[i].offset_minutes : timezones[i].offset_minutes);
        } else {
            ksprintf(off_str, 16, "%+d", timezones[i].offset_hours);
        }
        ksprintf(line, 80, "%-8s    %02d:%02d:%02d     UTC%s", timezones[i].name, th, tm, s, off_str);

        neo::display::set_cursor(5, 6 + i);
        if (i == 0) neo::display::set_color(14, 0);
        else neo::display::set_color(7, 0);
        neo::display::puts(line);
    }
    neo::display::set_color(7, 0);
}

static void draw_stopwatch_view() {
    int w = neo::display::get_width();

    neo::display::set_bold(true);
    neo::display::set_cursor((w - 10) / 2, 2);
    neo::display::puts("Stopwatch");
    neo::display::set_bold(false);

    unsigned int elapsed = state.sw_elapsed;
    if (state.sw_running) {
        elapsed += (neo::timer::get_ticks() - state.sw_start_ticks);
    }

    int total_cs = elapsed / 10; // assuming ticks ~ ms
    int mins = total_cs / 6000;
    int secs = (total_cs / 100) % 60;
    int cs = total_cs % 100;

    // Big display
    char tbuf[32];
    ksprintf(tbuf, 32, "%02d:%02d.%02d", mins, secs, cs);
    neo::display::set_color(14, 0);
    int cx = (w - neo_strlen(tbuf) * 2) / 2;
    // Draw with bigger spacing
    neo::display::set_cursor((w - 14) / 2, 5);
    neo::display::set_bold(true);
    kprintf("%02d : %02d . %02d", mins, secs, cs);
    neo::display::set_bold(false);
    neo::display::set_color(7, 0);

    // Controls
    neo::display::set_cursor(5, 8);
    neo::display::puts(state.sw_running ? "[S]top   [L]ap   [R]eset" : "[S]tart  [R]eset");

    // Laps
    if (state.lap_count > 0) {
        neo::display::set_cursor(5, 10);
        neo::display::set_bold(true);
        neo::display::puts("Laps:");
        neo::display::set_bold(false);

        int h = neo::display::get_height();
        int start = state.lap_count > (h - 12) ? state.lap_count - (h - 12) : 0;
        for (int i = start; i < state.lap_count; i++) {
            char lapbuf[48];
            ksprintf(lapbuf, 48, "  Lap %2d: %02d:%02d.%02d",
                     i + 1, state.laps[i].minutes, state.laps[i].seconds, state.laps[i].centiseconds);
            neo::display::set_cursor(5, 12 + i - start);
            neo::display::puts(lapbuf);
        }
    }
}

static void draw_timer_view() {
    int w = neo::display::get_width();

    neo::display::set_bold(true);
    neo::display::set_cursor((w - 15) / 2, 2);
    neo::display::puts("Countdown Timer");
    neo::display::set_bold(false);

    if (!state.timer_running && !state.timer_alarm) {
        // Setup mode
        char tbuf[32];
        ksprintf(tbuf, 32, "Set: %02d:%02d", state.timer_minutes, state.timer_seconds);
        neo::display::set_color(14, 0);
        neo::display::set_cursor((w - neo_strlen(tbuf)) / 2, 5);
        neo::display::puts(tbuf);
        neo::display::set_color(7, 0);

        neo::display::set_cursor(5, 8);
        neo::display::puts("Up/Down = Minutes   Left/Right = Seconds   Enter = Start");
    } else if (state.timer_alarm) {
        neo::display::set_color(12, 0);
        neo::display::set_bold(true);
        neo::display::set_cursor((w - 14) / 2, 5);
        neo::display::puts("** TIME UP! **");
        neo::display::set_bold(false);
        neo::display::set_color(7, 0);
        neo::display::set_cursor(5, 8);
        neo::display::puts("Press any key to reset");
    } else {
        // Running
        unsigned int elapsed = neo::timer::get_ticks() - state.timer_start_ticks;
        int remaining_ms = (int)state.timer_total_ms - (int)elapsed;
        if (remaining_ms <= 0) {
            state.timer_running = false;
            state.timer_alarm = true;
            // Beep
            neo::audio::init();
            neo::audio::play_tone(0, 880, 500);
            return;
        }

        int rem_secs = remaining_ms / 1000;
        int rem_min = rem_secs / 60;
        int rem_sec = rem_secs % 60;

        char tbuf[32];
        ksprintf(tbuf, 32, "%02d:%02d", rem_min, rem_sec);
        int cx = (w - 10) / 2;
        draw_big_time(cx - 10, 4, 0, rem_min, rem_sec);

        neo::display::set_cursor(5, 13);
        neo::display::puts("Press Esc to cancel");
    }
}

static void draw_analog_view() {
    int h_time, m_time, s_time, year, month, day;
    get_rtc_time(h_time, m_time, s_time, year, month, day);
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    neo::display::set_bold(true);
    neo::display::set_cursor((w - 12) / 2, 1);
    neo::display::puts("Analog Clock");
    neo::display::set_bold(false);

    // Draw clock face - radius 8 chars
    int cx = w / 2;
    int cy = h / 2;
    int radius = 8;

    // Draw circle outline using characters
    // Place hour markers
    for (int hour = 1; hour <= 12; hour++) {
        double angle = (hour * 30.0 - 90.0) * PI_VAL / 180.0;
        // sin/cos approximation
        double sa = 0, ca = 0;
        // Taylor series sin
        double term = angle;
        sa = term;
        for (int k = 1; k < 10; k++) {
            term *= -angle * angle / ((2 * k) * (2 * k + 1));
            sa += term;
        }
        ca = 1.0;
        term = 1.0;
        double a2 = angle;
        for (int k = 1; k < 10; k++) {
            term *= -a2 * a2 / ((2 * k - 1) * (2 * k));
            ca += term;
        }

        int px = cx + (int)(ca * radius * 2); // *2 for aspect ratio
        int py = cy + (int)(sa * radius);

        neo::display::set_cursor(px, py);
        neo::display::set_color(15, 0);
        if (hour >= 10) {
            neo::display::putchar('0' + hour / 10);
            neo::display::putchar('0' + hour % 10);
        } else {
            neo::display::putchar('0' + hour);
        }
    }

    // Draw hands as lines of characters
    // Hour hand (short)
    {
        double angle = ((h_time % 12) * 30.0 + m_time * 0.5 - 90.0) * PI_VAL / 180.0;
        double sa = 0, ca = 0, term;
        term = angle; sa = term;
        for (int k = 1; k < 10; k++) { term *= -angle * angle / ((2*k)*(2*k+1)); sa += term; }
        ca = 1.0; term = 1.0;
        for (int k = 1; k < 10; k++) { term *= -angle*angle/((2*k-1)*(2*k)); ca += term; }

        neo::display::set_color(14, 0);
        for (int r = 1; r <= 4; r++) {
            int px = cx + (int)(ca * r * 2);
            int py = cy + (int)(sa * r);
            neo::display::set_cursor(px, py);
            neo::display::putchar('#');
        }
    }

    // Minute hand (longer)
    {
        double angle = (m_time * 6.0 - 90.0) * PI_VAL / 180.0;
        double sa = 0, ca = 0, term;
        term = angle; sa = term;
        for (int k = 1; k < 10; k++) { term *= -angle * angle / ((2*k)*(2*k+1)); sa += term; }
        ca = 1.0; term = 1.0;
        for (int k = 1; k < 10; k++) { term *= -angle*angle/((2*k-1)*(2*k)); ca += term; }

        neo::display::set_color(10, 0);
        for (int r = 1; r <= 6; r++) {
            int px = cx + (int)(ca * r * 2);
            int py = cy + (int)(sa * r);
            neo::display::set_cursor(px, py);
            neo::display::putchar('*');
        }
    }

    // Second hand
    {
        double angle = (s_time * 6.0 - 90.0) * PI_VAL / 180.0;
        double sa = 0, ca = 0, term;
        term = angle; sa = term;
        for (int k = 1; k < 10; k++) { term *= -angle * angle / ((2*k)*(2*k+1)); sa += term; }
        ca = 1.0; term = 1.0;
        for (int k = 1; k < 10; k++) { term *= -angle*angle/((2*k-1)*(2*k)); ca += term; }

        neo::display::set_color(12, 0);
        for (int r = 1; r <= 7; r++) {
            int px = cx + (int)(ca * r * 2);
            int py = cy + (int)(sa * r);
            neo::display::set_cursor(px, py);
            neo::display::putchar('.');
        }
    }

    // Center dot
    neo::display::set_color(15, 0);
    neo::display::set_cursor(cx, cy);
    neo::display::putchar('O');

    // Digital time below
    char dtbuf[32];
    ksprintf(dtbuf, 32, "%02d:%02d:%02d", h_time, m_time, s_time);
    neo::display::set_color(11, 0);
    neo::display::set_cursor((w - 8) / 2, h - 4);
    neo::display::puts(dtbuf);
    neo::display::set_color(7, 0);
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
    neo::display::puts("NeoClock");
    neo::display::set_bold(false);

    // Tab bar
    const char* tabs[] = {"[1]Clock", "[2]World", "[3]Stop", "[4]Timer", "[5]Analog"};
    int tx = 20;
    for (int i = 0; i < 5; i++) {
        neo::display::set_cursor(tx, 0);
        if ((int)state.view == i) neo::display::set_color(14, 1);
        else neo::display::set_color(7, 1);
        neo::display::puts(tabs[i]);
        tx += neo_strlen(tabs[i]) + 2;
    }
    neo::display::set_color(7, 0);

    switch (state.view) {
        case VIEW_CLOCK:     draw_clock_view(); break;
        case VIEW_WORLD:     draw_world_view(); break;
        case VIEW_STOPWATCH: draw_stopwatch_view(); break;
        case VIEW_TIMER:     draw_timer_view(); break;
        case VIEW_ANALOG:    draw_analog_view(); break;
    }

    // Status bar
    neo::display::set_color(0, 7);
    neo::display::set_cursor(0, h - 1);
    for (int i = 0; i < w; i++) neo::display::putchar(' ');
    neo::display::set_cursor(1, h - 1);
    neo::display::puts("1-5=View  Esc=Quit");
    neo::display::set_color(7, 0);
}

static void handle_key(unsigned char sc) {
    bool shift = neo::keyboard::is_shift_down();
    char ch = neo::keyboard::translate(sc, shift);

    if (sc == 0x01) { state.running = false; return; }

    // Number keys for views
    if (ch == '1') { state.view = VIEW_CLOCK; return; }
    if (ch == '2') { state.view = VIEW_WORLD; return; }
    if (ch == '3') { state.view = VIEW_STOPWATCH; return; }
    if (ch == '4') { state.view = VIEW_TIMER; return; }
    if (ch == '5') { state.view = VIEW_ANALOG; return; }

    // Stopwatch controls
    if (state.view == VIEW_STOPWATCH) {
        if (ch == 's' || ch == 'S') {
            if (state.sw_running) {
                state.sw_elapsed += (neo::timer::get_ticks() - state.sw_start_ticks);
                state.sw_running = false;
            } else {
                state.sw_start_ticks = neo::timer::get_ticks();
                state.sw_running = true;
            }
        }
        if ((ch == 'l' || ch == 'L') && state.sw_running && state.lap_count < MAX_LAPS) {
            unsigned int elapsed = state.sw_elapsed + (neo::timer::get_ticks() - state.sw_start_ticks);
            int cs = elapsed / 10;
            state.laps[state.lap_count].minutes = cs / 6000;
            state.laps[state.lap_count].seconds = (cs / 100) % 60;
            state.laps[state.lap_count].centiseconds = cs % 100;
            state.laps[state.lap_count].ticks = elapsed;
            state.lap_count++;
        }
        if (ch == 'r' || ch == 'R') {
            state.sw_running = false;
            state.sw_elapsed = 0;
            state.lap_count = 0;
        }
    }

    // Timer controls
    if (state.view == VIEW_TIMER) {
        if (state.timer_alarm) {
            state.timer_alarm = false;
            return;
        }
        if (!state.timer_running) {
            if (sc == 0x4C) state.timer_minutes = (state.timer_minutes + 1) % 100; // Up
            if (sc == 0x4D) state.timer_minutes = state.timer_minutes > 0 ? state.timer_minutes - 1 : 99; // Down
            if (sc == 0x4E) state.timer_seconds = (state.timer_seconds + 5) % 60; // Right
            if (sc == 0x4F) state.timer_seconds = state.timer_seconds >= 5 ? state.timer_seconds - 5 : 55; // Left
            if (ch == '\r' || ch == '\n' || sc == 0x44) {
                state.timer_total_ms = (state.timer_minutes * 60 + state.timer_seconds) * 1000;
                if (state.timer_total_ms > 0) {
                    state.timer_start_ticks = neo::timer::get_ticks();
                    state.timer_running = true;
                }
            }
        }
    }
}

} // anonymous namespace

extern "C" void app_main(int argc, char** argv) {
    neo_memset(&state, 0, sizeof(state));
    state.running = true;
    state.view = VIEW_CLOCK;
    state.timer_minutes = 5;

    while (state.running) {
        draw_ui();

        // Poll for input without blocking too long
        for (int i = 0; i < 50; i++) {
            if (neo::keyboard::key_available()) {
                unsigned char sc = neo::keyboard::read_scancode();
                handle_key(sc);
                break;
            }
            neo::timer::delay_ms(20);
        }
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    kprintf("NeoClock exited.\n");
}
