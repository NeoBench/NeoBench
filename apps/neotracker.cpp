#include "../include/neobench.h"
#include "../lib/string.h"

// NeoTracker - 4-channel MOD-style music tracker
// Pattern editor, playback via Paula, song structure, save/load .nmt

namespace neotracker {

static const int NUM_CHANNELS = 4;
static const int ROWS_PER_PATTERN = 64;
static const int MAX_PATTERNS = 32;
static const int MAX_ORDER = 128;
static const int MAX_INSTRUMENTS = 16;
static const int VISIBLE_ROWS = 24;

// Note definitions: C-1=0 to B-7=83, 0xFF = no note
static const char* note_names[] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

// Note frequencies (octave 4 base, shifted for others)
static const int base_freqs[] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};

struct Note {
    unsigned char note;       // 0xFF = empty, 0-83 = note
    unsigned char instrument; // 0 = none, 1-16
    unsigned char volume;     // 0-64, 0xFF = no change
    unsigned char effect;     // 0 = none
    unsigned char effect_val; // effect parameter
};

struct Pattern {
    Note rows[ROWS_PER_PATTERN][NUM_CHANNELS];

    void clear() {
        for (int r = 0; r < ROWS_PER_PATTERN; r++)
            for (int c = 0; c < NUM_CHANNELS; c++) {
                rows[r][c].note = 0xFF;
                rows[r][c].instrument = 0;
                rows[r][c].volume = 0xFF;
                rows[r][c].effect = 0;
                rows[r][c].effect_val = 0;
            }
    }
};

struct Instrument {
    char name[16];
    unsigned char volume;
    bool active;
};

struct Song {
    char title[32];
    Pattern patterns[MAX_PATTERNS];
    int order[MAX_ORDER];
    int order_length;
    int num_patterns;
    Instrument instruments[MAX_INSTRUMENTS];
    int tempo;  // BPM
    int speed;  // ticks per row

    void init() {
        neo_strcpy(title, "Untitled");
        num_patterns = 1;
        order_length = 1;
        order[0] = 0;
        tempo = 125;
        speed = 6;
        for (int i = 0; i < MAX_PATTERNS; i++) patterns[i].clear();
        for (int i = 0; i < MAX_INSTRUMENTS; i++) {
            ksprintf(instruments[i].name, 16, "Inst %02d", i + 1);
            instruments[i].volume = 64;
            instruments[i].active = (i == 0);
        }
    }
};

struct TrackerApp {
    Song song;
    int cur_pattern;
    int cur_row;
    int cur_channel;
    int cur_field; // 0=note, 1=inst, 2=vol, 3=effect
    int scroll_row;
    int cur_order_pos;
    bool playing;
    bool running;
    int screen_w, screen_h;
    bool channel_mute[NUM_CHANNELS];
    bool channel_solo[NUM_CHANNELS];
    int edit_octave;
    char status_msg[64];
    int status_timer;
    unsigned int play_tick;
    int play_row;
    int play_order;

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        song.init();
        cur_pattern = 0;
        cur_row = 0;
        cur_channel = 0;
        cur_field = 0;
        scroll_row = 0;
        cur_order_pos = 0;
        playing = false;
        running = true;
        edit_octave = 4;
        status_msg[0] = '\0';
        status_timer = 0;
        play_tick = 0;
        play_row = 0;
        play_order = 0;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            channel_mute[i] = false;
            channel_solo[i] = false;
        }
        neo::audio::init();
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    int note_to_freq(unsigned char note) {
        if (note == 0xFF || note > 83) return 0;
        int octave = note / 12 + 1;
        int semitone = note % 12;
        int freq = base_freqs[semitone];
        int target_oct = octave;
        // Shift from octave 4
        while (target_oct < 4) { freq /= 2; target_oct++; }
        while (target_oct > 4) { freq *= 2; target_oct--; }
        return freq;
    }

    void format_note(unsigned char note, char* buf) {
        if (note == 0xFF) {
            neo_strcpy(buf, "...");
            return;
        }
        int octave = note / 12 + 1;
        int semi = note % 12;
        buf[0] = note_names[semi][0];
        buf[1] = note_names[semi][1];
        buf[2] = '0' + octave;
        buf[3] = '\0';
    }

    void render_header() {
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[128];
        ksprintf(hdr, sizeof(hdr), " NeoTracker | \"%s\" | Pat:%02d Ord:%d/%d | BPM:%d Spd:%d | Oct:%d | %s ",
                 song.title, cur_pattern, cur_order_pos + 1, song.order_length,
                 song.tempo, song.speed, edit_octave,
                 playing ? "PLAY" : "STOP");
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }
    }

    void render_channel_headers() {
        neo::display::set_color(0, 6);
        neo::display::set_cursor(0, 1);
        neo::display::puts("Row ");
        for (int c = 0; c < NUM_CHANNELS; c++) {
            char chdr[20];
            ksprintf(chdr, sizeof(chdr), "| Ch%d%s ", c + 1,
                     channel_mute[c] ? "(M)" : "    ");
            neo::display::puts(chdr);
        }
        for (int i = 60; i < screen_w; i++) {
            neo::display::set_cursor(i, 1);
            neo::display::putchar(' ');
        }
    }

    void render_pattern() {
        Pattern& pat = song.patterns[cur_pattern];
        int start_y = 2;
        int visible = VISIBLE_ROWS;
        if (start_y + visible > screen_h - 4) visible = screen_h - 4 - start_y;

        for (int vi = 0; vi < visible; vi++) {
            int row = scroll_row + vi;
            int sy = start_y + vi;
            if (row >= ROWS_PER_PATTERN) break;

            // Row number
            bool is_current = (row == cur_row);
            bool is_playing = (playing && row == play_row);

            if (is_playing) neo::display::set_color(0, 2);
            else if (is_current) neo::display::set_color(0, 7);
            else if (row % 16 == 0) neo::display::set_color(3, 0);
            else if (row % 4 == 0) neo::display::set_color(6, 0);
            else neo::display::set_color(7, 0);

            neo::display::set_cursor(0, sy);
            char rnum[6];
            ksprintf(rnum, sizeof(rnum), " %02d ", row);
            neo::display::puts(rnum);

            // Channels
            for (int c = 0; c < NUM_CHANNELS; c++) {
                Note& n = pat.rows[row][c];
                bool is_cell = (is_current && c == cur_channel);

                neo::display::set_cursor(4 + c * 14, sy);

                if (channel_mute[c]) neo::display::set_color(1, 0);
                else if (is_cell) neo::display::set_color(0, 7);
                else if (is_playing) neo::display::set_color(0, 2);
                else if (is_current) neo::display::set_color(0, 6);
                else neo::display::set_color(7, 0);

                neo::display::putchar('|');

                // Note
                char nbuf[4];
                format_note(n.note, nbuf);
                if (is_cell && cur_field == 0) neo::display::set_color(0, 5);
                else if (n.note != 0xFF) neo::display::set_fg(2);
                neo::display::puts(nbuf);

                neo::display::putchar(' ');

                // Instrument
                if (is_cell && cur_field == 1) neo::display::set_color(0, 5);
                else if (is_cell) neo::display::set_color(0, 7);
                else neo::display::set_fg(6);
                if (n.instrument > 0) {
                    char ibuf[3];
                    ksprintf(ibuf, sizeof(ibuf), "%02d", n.instrument);
                    neo::display::puts(ibuf);
                } else {
                    neo::display::puts("..");
                }

                neo::display::putchar(' ');

                // Volume
                if (is_cell && cur_field == 2) neo::display::set_color(0, 5);
                else if (is_cell) neo::display::set_color(0, 7);
                else neo::display::set_fg(3);
                if (n.volume != 0xFF) {
                    char vbuf[3];
                    ksprintf(vbuf, sizeof(vbuf), "%02d", n.volume);
                    neo::display::puts(vbuf);
                } else {
                    neo::display::puts("..");
                }

                neo::display::putchar(' ');

                // Effect
                if (is_cell && cur_field == 3) neo::display::set_color(0, 5);
                else if (is_cell) neo::display::set_color(0, 7);
                else neo::display::set_fg(4);
                if (n.effect > 0) {
                    char ebuf[4];
                    ksprintf(ebuf, sizeof(ebuf), "%X%02X", n.effect, n.effect_val);
                    neo::display::puts(ebuf);
                } else {
                    neo::display::puts("...");
                }
            }
        }
    }

    void render_order_list() {
        int ox = screen_w - 16;
        if (ox < 60) return;
        neo::display::set_color(0, 6);
        neo::display::set_cursor(ox, 1);
        neo::display::puts("  Order List  ");

        for (int i = 0; i < song.order_length && i < VISIBLE_ROWS; i++) {
            int sy = 2 + i;
            if (sy >= screen_h - 4) break;
            if (i == cur_order_pos) neo::display::set_color(0, 7);
            else neo::display::set_color(7, 0);
            neo::display::set_cursor(ox, sy);
            char obuf[16];
            ksprintf(obuf, sizeof(obuf), " %02d: Pat %02d ", i, song.order[i]);
            neo::display::puts(obuf);
        }
    }

    void render_status() {
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 2);
        char help[128];
        if (status_timer > 0) {
            ksprintf(help, sizeof(help), " %s", status_msg);
        } else {
            neo_strcpy(help, " F5:Play F8:Stop Tab:Field </>:Ch +/-:Oct M:Mute Enter:Note Del:Clear Q:Quit");
        }
        neo::display::puts(help);
        for (int i = neo_strlen(help); i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 2);
            neo::display::putchar(' ');
        }

        // VU meters when playing
        if (playing) {
            neo::display::set_color(7, 0);
            neo::display::set_cursor(0, screen_h - 1);
            for (int c = 0; c < NUM_CHANNELS; c++) {
                char vu[16];
                ksprintf(vu, sizeof(vu), "Ch%d:", c + 1);
                neo::display::puts(vu);
                // Simple VU based on whether there's a note
                Note& n = song.patterns[cur_pattern].rows[play_row][c];
                int level = (n.note != 0xFF && !channel_mute[c]) ? 8 : 0;
                neo::display::set_fg(2);
                for (int i = 0; i < level; i++) neo::display::putchar('=');
                for (int i = level; i < 8; i++) neo::display::putchar(' ');
                neo::display::set_fg(7);
                neo::display::puts("  ");
            }
        }
    }

    void render() {
        neo::display::clear();
        render_header();
        render_channel_headers();
        render_pattern();
        render_order_list();
        render_status();
    }

    void scroll_to_cursor() {
        if (cur_row < scroll_row) scroll_row = cur_row;
        if (cur_row >= scroll_row + VISIBLE_ROWS) scroll_row = cur_row - VISIBLE_ROWS + 1;
    }

    // Map keyboard to notes (ZSXDCVGBHNJM = C4-B4, Q2W3ER5T6Y7U = C5-B5)
    int key_to_note(char ch) {
        int base = edit_octave * 12;
        switch (ch) {
            case 'z': case 'Z': return base + 0;
            case 's': case 'S': return base + 1;
            case 'x': case 'X': return base + 2;
            case 'd': case 'D': return base + 3;
            case 'c': case 'C': return base + 4;
            case 'v': case 'V': return base + 5;
            case 'g': case 'G': return base + 6;
            case 'b': case 'B': return base + 7;
            case 'h': case 'H': return base + 8;
            case 'n': case 'N': return base + 9;
            case 'j': case 'J': return base + 10;
            case 'm': case 'M': return base + 11;
            case 'q': case 'Q': return base + 12;
            case '2': return base + 13;
            case 'w': case 'W': return base + 14;
            case '3': return base + 15;
            case 'e': case 'E': return base + 16;
            case 'r': case 'R': return base + 17;
            case '5': return base + 18;
            case 't': case 'T': return base + 19;
            case '6': return base + 20;
            case 'y': case 'Y': return base + 21;
            case '7': return base + 22;
            case 'u': case 'U': return base + 23;
            default: return -1;
        }
    }

    void play_current_row() {
        Pattern& pat = song.patterns[song.order[play_order]];
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (channel_mute[c]) continue;
            Note& n = pat.rows[play_row][c];
            if (n.note != 0xFF) {
                int freq = note_to_freq(n.note);
                int dur = (60000 / song.tempo) * song.speed / 6;
                if (dur < 20) dur = 20;
                neo::audio::play_tone(c, freq, dur);
            }
        }
    }

    void advance_playback() {
        unsigned int now = neo::timer::get_ticks();
        int ms_per_row = (60000 / song.tempo) * song.speed / 6;
        unsigned int ticks_per_row = ms_per_row / 20; // approximate
        if (ticks_per_row < 1) ticks_per_row = 1;

        if (now - play_tick >= ticks_per_row) {
            play_tick = now;
            play_current_row();
            play_row++;
            if (play_row >= ROWS_PER_PATTERN) {
                play_row = 0;
                play_order++;
                if (play_order >= song.order_length) play_order = 0;
            }
            cur_pattern = song.order[play_order];
        }
    }

    void save_song(const char* path) {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, path, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) == 0) {
            // Header
            neo::filesystem::write(fh, "NMT1", 4);
            neo::filesystem::write(fh, song.title, 32);
            neo::filesystem::write(fh, (char*)&song.num_patterns, 4);
            neo::filesystem::write(fh, (char*)&song.order_length, 4);
            neo::filesystem::write(fh, (char*)&song.tempo, 4);
            neo::filesystem::write(fh, (char*)&song.speed, 4);
            neo::filesystem::write(fh, (char*)song.order, song.order_length * 4);
            // Patterns
            for (int p = 0; p < song.num_patterns; p++) {
                neo::filesystem::write(fh, (char*)&song.patterns[p],
                    ROWS_PER_PATTERN * NUM_CHANNELS * sizeof(Note));
            }
            neo::filesystem::close(fh);
            set_status("Song saved!");
        } else {
            set_status("Save failed!");
        }
    }

    void load_song(const char* path) {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) == 0) {
            char magic[4];
            neo::filesystem::read(fh, magic, 4);
            if (neo_strncmp(magic, "NMT1", 4) == 0) {
                neo::filesystem::read(fh, song.title, 32);
                neo::filesystem::read(fh, (char*)&song.num_patterns, 4);
                neo::filesystem::read(fh, (char*)&song.order_length, 4);
                neo::filesystem::read(fh, (char*)&song.tempo, 4);
                neo::filesystem::read(fh, (char*)&song.speed, 4);
                neo::filesystem::read(fh, (char*)song.order, song.order_length * 4);
                for (int p = 0; p < song.num_patterns; p++) {
                    neo::filesystem::read(fh, (char*)&song.patterns[p],
                        ROWS_PER_PATTERN * NUM_CHANNELS * sizeof(Note));
                }
                set_status("Song loaded!");
            }
            neo::filesystem::close(fh);
        } else {
            set_status("Load failed!");
        }
    }

    void handle_key(unsigned char sc) {
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        // Arrow keys
        if (sc == 0x4C) { if (cur_row > 0) cur_row--; scroll_to_cursor(); return; }
        if (sc == 0x4D) { if (cur_row < ROWS_PER_PATTERN - 1) cur_row++; scroll_to_cursor(); return; }
        if (sc == 0x4F) { // Left - previous field/channel
            if (cur_field > 0) cur_field--;
            else if (cur_channel > 0) { cur_channel--; cur_field = 3; }
            return;
        }
        if (sc == 0x50) { // Right - next field/channel
            if (cur_field < 3) cur_field++;
            else if (cur_channel < NUM_CHANNELS - 1) { cur_channel++; cur_field = 0; }
            return;
        }

        // Tab - cycle channels
        if (sc == 0x42) {
            cur_channel = (cur_channel + 1) % NUM_CHANNELS;
            return;
        }

        // Function keys via scancode
        if (sc == 0x54) { // F5 - Play
            playing = true;
            play_row = 0;
            play_order = cur_order_pos;
            play_tick = neo::timer::get_ticks();
            set_status("Playing...");
            return;
        }
        if (sc == 0x57) { // F8 - Stop
            playing = false;
            set_status("Stopped");
            return;
        }

        // Delete - clear current cell
        if (sc == 0x46) {
            Note& n = song.patterns[cur_pattern].rows[cur_row][cur_channel];
            n.note = 0xFF; n.instrument = 0; n.volume = 0xFF; n.effect = 0; n.effect_val = 0;
            if (cur_row < ROWS_PER_PATTERN - 1) cur_row++;
            scroll_to_cursor();
            return;
        }

        // +/- change octave
        if (ch == '+' || ch == '=') { if (edit_octave < 7) edit_octave++; return; }
        if (ch == '-') { if (edit_octave > 1) edit_octave--; return; }

        // < > change pattern
        if (ch == ',') { if (cur_pattern > 0) cur_pattern--; return; }
        if (ch == '.') {
            if (cur_pattern < song.num_patterns - 1) cur_pattern++;
            else if (cur_pattern < MAX_PATTERNS - 1) {
                song.num_patterns++;
                cur_pattern++;
            }
            return;
        }

        // M - mute channel
        if (ch == 'm' || ch == 'M') {
            if (shift) {
                // Solo
                bool all_muted = true;
                for (int i = 0; i < NUM_CHANNELS; i++) {
                    if (i != cur_channel && !channel_mute[i]) all_muted = false;
                }
                if (all_muted) {
                    for (int i = 0; i < NUM_CHANNELS; i++) channel_mute[i] = false;
                } else {
                    for (int i = 0; i < NUM_CHANNELS; i++) channel_mute[i] = (i != cur_channel);
                }
            } else {
                channel_mute[cur_channel] = !channel_mute[cur_channel];
            }
            return;
        }

        // Escape = quit
        if (sc == 0x45) { running = false; return; }

        // F2 = save, F3 = load
        if (sc == 0x51) { save_song("song.nmt"); return; }
        if (sc == 0x52) { load_song("song.nmt"); return; }

        // Note entry on note field
        if (cur_field == 0 && !playing) {
            int note = key_to_note(ch);
            if (note >= 0 && note <= 83) {
                song.patterns[cur_pattern].rows[cur_row][cur_channel].note = (unsigned char)note;
                if (song.patterns[cur_pattern].rows[cur_row][cur_channel].instrument == 0)
                    song.patterns[cur_pattern].rows[cur_row][cur_channel].instrument = 1;
                // Play preview
                int freq = note_to_freq((unsigned char)note);
                neo::audio::play_tone(cur_channel, freq, 150);
                // Advance
                if (cur_row < ROWS_PER_PATTERN - 1) cur_row++;
                scroll_to_cursor();
            }
        }
    }

    void run() {
        init();

        while (running) {
            render();
            if (status_timer > 0) status_timer--;

            if (playing) advance_playback();

            int wait = 0;
            while (!neo::keyboard::key_available() && wait < 5) {
                neo::timer::delay_ms(10);
                wait++;
                if (playing) advance_playback();
            }

            if (neo::keyboard::key_available()) {
                unsigned char sc = neo::keyboard::read_scancode();
                if (!(sc & 0x80)) handle_key(sc);
            }
        }

        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoTracker: Goodbye.\n");
    }
};

} // namespace neotracker

extern "C" void app_main(int argc, char** argv) {
    neotracker::TrackerApp app;
    app.run();
}
