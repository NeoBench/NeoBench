#include "../include/neobench.h"
#include "../lib/string.h"

// NeoPlayer - Paula Audio Player with MOD support
// File browser, visualizer, playlist, playback controls

namespace neoplayer {

static const int MAX_FILES = 128;
static const int MAX_PLAYLIST = 32;
static const int NUM_CHANNELS = 4;
static const int VU_HEIGHT = 16;
static const int WAVE_WIDTH = 60;
static const int PATTERN_ROWS = 64;

struct FileEntry {
    char name[64];
    int size;
    bool is_dir;
};

struct ChannelState {
    int frequency;
    int volume;     // 0-64
    int period;
    int note;       // 0-83
    int instrument;
    bool active;
    int vu_level;   // 0-VU_HEIGHT
    int wave_phase;
};

struct ModuleInfo {
    char title[21];
    int num_patterns;
    int num_samples;
    int song_length;
    unsigned char order[128];
    int current_pattern;
    int current_row;
    int current_order;
    int bpm;
    int speed;
    bool loaded;
};

struct PlaylistEntry {
    char path[INODE_SIZE];
    char name[64];
    bool valid;
};

enum PlayerState {
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_PAUSED
};

enum ViewMode {
    VIEW_BROWSER,
    VIEW_PLAYER,
    VIEW_PLAYLIST
};

static const char* note_names[] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

static const int base_freqs[] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};

struct PlayerApp {
    int screen_w, screen_h;
    bool running;
    PlayerState state;
    ViewMode view;

    // File browser
    FileEntry files[MAX_FILES];
    int num_files;
    int file_selected;
    int file_scroll;
    char current_path[INODE_SIZE];

    // Module
    ModuleInfo module;
    ChannelState channels[NUM_CHANNELS];

    // Playlist
    PlaylistEntry playlist[MAX_PLAYLIST];
    int playlist_count;
    int playlist_current;

    // Playback
    unsigned int play_tick;
    int play_row;
    bool loop_mode;
    int master_volume; // 0-100

    // Visualizer
    int vu_decay_timer;

    char status_msg[64];
    int status_timer;

    unsigned int rng_state;

    unsigned int rng() {
        rng_state = rng_state * 1103515245 + 12345;
        return (rng_state >> 16) & 0x7FFF;
    }

    void init() {
        screen_w = neo::display::get_width();
        screen_h = neo::display::get_height();
        running = true;
        state = STATE_STOPPED;
        view = VIEW_BROWSER;
        num_files = 0;
        file_selected = 0;
        file_scroll = 0;
        neo_strcpy(current_path, "DF0:");
        module.loaded = false;
        module.bpm = 125;
        module.speed = 6;
        playlist_count = 0;
        playlist_current = -1;
        play_tick = 0;
        play_row = 0;
        loop_mode = true;
        master_volume = 80;
        vu_decay_timer = 0;
        status_msg[0] = '\0';
        status_timer = 0;
        rng_state = neo::timer::get_ticks();
        for (int i = 0; i < NUM_CHANNELS; i++) {
            channels[i].frequency = 0;
            channels[i].volume = 0;
            channels[i].active = false;
            channels[i].vu_level = 0;
            channels[i].wave_phase = 0;
            channels[i].note = -1;
            channels[i].instrument = 0;
        }
        neo::audio::init();
    }

    void set_status(const char* msg) {
        neo_strcpy(status_msg, msg);
        status_timer = 80;
    }

    int note_to_freq(int note) {
        if (note < 0 || note > 83) return 0;
        int octave = note / 12 + 1;
        int semi = note % 12;
        int freq = base_freqs[semi];
        int target = octave;
        while (target < 4) { freq /= 2; target++; }
        while (target > 4) { freq *= 2; target--; }
        return freq;
    }

    void format_note(int note, char* buf) {
        if (note < 0 || note > 83) { neo_strcpy(buf, "---"); return; }
        int octave = note / 12 + 1;
        int semi = note % 12;
        buf[0] = note_names[semi][0];
        buf[1] = note_names[semi][1];
        buf[2] = '0' + octave;
        buf[3] = '\0';
    }

    void scan_directory() {
        neo::filesystem::DirEntry entries[MAX_FILES];
        int count = neo::filesystem::readdir(current_path, entries, MAX_FILES);
        num_files = 0;

        if (neo_strlen(current_path) > 1) {
            neo_strcpy(files[num_files].name, "..");
            files[num_files].size = 0;
            files[num_files].is_dir = true;
            num_files++;
        }

        for (int i = 0; i < count && num_files < MAX_FILES; i++) {
            neo_strncpy(files[num_files].name, entries[i].name, 63);
            files[num_files].name[63] = '\0';
            files[num_files].size = entries[i].size;
            files[num_files].is_dir = (entries[i].type == 1);
            num_files++;
        }
        file_selected = 0;
        file_scroll = 0;
    }

    bool is_mod_file(const char* name) {
        int len = neo_strlen(name);
        if (len < 4) return false;
        if (neo_strncmp(name + len - 4, ".mod", 4) == 0) return true;
        if (neo_strncmp(name + len - 4, ".MOD", 4) == 0) return true;
        if (neo_strncmp(name + len - 4, ".s3m", 4) == 0) return true;
        if (neo_strncmp(name + len - 4, ".nmt", 4) == 0) return true;
        return false;
    }

    bool load_module(const char* path) {
        neo::filesystem::FileHandle fh;
        if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) {
            set_status("Cannot open file!");
            return false;
        }

        // Try to read MOD header
        char header[1084];
        int rd = neo::filesystem::read(fh, header, 1084);
        neo::filesystem::close(fh);

        if (rd < 20) {
            // Generate demo module
            generate_demo_module(path);
            return true;
        }

        // Check for MOD magic at offset 1080
        bool is_mod = false;
        if (rd >= 1084) {
            if (neo_strncmp(header + 1080, "M.K.", 4) == 0 ||
                neo_strncmp(header + 1080, "M!K!", 4) == 0 ||
                neo_strncmp(header + 1080, "4CHN", 4) == 0) {
                is_mod = true;
            }
        }

        if (is_mod) {
            // Parse MOD title
            neo_memcpy(module.title, header, 20);
            module.title[20] = '\0';
            module.num_samples = 31;
            module.song_length = (unsigned char)header[950];
            if (module.song_length > 128) module.song_length = 128;
            for (int i = 0; i < 128; i++) {
                module.order[i] = (unsigned char)header[952 + i];
            }
            // Find highest pattern
            module.num_patterns = 0;
            for (int i = 0; i < module.song_length; i++) {
                if (module.order[i] > module.num_patterns) module.num_patterns = module.order[i];
            }
            module.num_patterns++;
            module.bpm = 125;
            module.speed = 6;
            module.loaded = true;
        } else {
            generate_demo_module(path);
        }

        module.current_pattern = 0;
        module.current_row = 0;
        module.current_order = 0;
        return true;
    }

    void generate_demo_module(const char* path) {
        // Generate a procedural demo module for playback
        unsigned int seed = 0;
        for (int i = 0; path[i]; i++) seed = seed * 31 + path[i];
        rng_state = seed;

        // Extract filename for title
        const char* name = path;
        for (const char* p = path; *p; p++) {
            if (*p == '/' || *p == ':') name = p + 1;
        }
        neo_strncpy(module.title, name, 20);
        module.title[20] = '\0';
        module.num_patterns = 4;
        module.num_samples = 8;
        module.song_length = 4;
        for (int i = 0; i < 4; i++) module.order[i] = i;
        module.bpm = 120 + (rng() % 40);
        module.speed = 4 + (rng() % 4);
        module.loaded = true;
    }

    void play_step() {
        if (state != STATE_PLAYING || !module.loaded) return;

        unsigned int now = neo::timer::get_ticks();
        int ms_per_row = (60000 / module.bpm) * module.speed / 24;
        if (ms_per_row < 20) ms_per_row = 20;
        unsigned int ticks_per_row = ms_per_row / 20;
        if (ticks_per_row < 1) ticks_per_row = 1;

        if (now - play_tick >= ticks_per_row) {
            play_tick = now;

            // Generate notes for current row
            unsigned int seed = play_row * 17 + module.current_order * 997;
            rng_state ^= seed;

            for (int c = 0; c < NUM_CHANNELS; c++) {
                // Procedural note generation
                int r = rng() % 100;
                if (r < 30) { // 30% chance of a note
                    int scale[] = {0, 2, 4, 5, 7, 9, 11}; // major scale
                    int base_oct = 3 + (c % 2);
                    int note = base_oct * 12 + scale[rng() % 7];
                    if (note > 83) note = 83;

                    channels[c].note = note;
                    channels[c].frequency = note_to_freq(note);
                    channels[c].volume = 40 + (rng() % 25);
                    channels[c].active = true;
                    channels[c].instrument = 1 + (rng() % module.num_samples);
                    channels[c].vu_level = VU_HEIGHT;

                    int vol = (channels[c].volume * master_volume) / 100;
                    int dur = ms_per_row;
                    neo::audio::play_tone(c, channels[c].frequency, dur);
                } else if (r < 40) { // 10% chance of note off
                    channels[c].active = false;
                    channels[c].note = -1;
                }
            }

            module.current_row = play_row;
            play_row++;
            if (play_row >= PATTERN_ROWS) {
                play_row = 0;
                module.current_order++;
                if (module.current_order >= module.song_length) {
                    if (loop_mode) {
                        module.current_order = 0;
                    } else {
                        state = STATE_STOPPED;
                        // Try next playlist entry
                        play_next();
                        return;
                    }
                }
                module.current_pattern = module.order[module.current_order];
            }
        }
    }

    void decay_vu() {
        for (int c = 0; c < NUM_CHANNELS; c++) {
            if (channels[c].vu_level > 0) channels[c].vu_level--;
            channels[c].wave_phase += channels[c].frequency / 50;
        }
    }

    void play_current() {
        state = STATE_PLAYING;
        play_row = 0;
        play_tick = neo::timer::get_ticks();
        module.current_order = 0;
        module.current_row = 0;
        set_status("Playing...");
    }

    void stop_playback() {
        state = STATE_STOPPED;
        for (int c = 0; c < NUM_CHANNELS; c++) {
            channels[c].active = false;
            channels[c].vu_level = 0;
        }
        set_status("Stopped");
    }

    void toggle_pause() {
        if (state == STATE_PLAYING) { state = STATE_PAUSED; set_status("Paused"); }
        else if (state == STATE_PAUSED) { state = STATE_PLAYING; set_status("Playing..."); }
    }

    void play_next() {
        if (playlist_count == 0) return;
        playlist_current++;
        if (playlist_current >= playlist_count) playlist_current = 0;
        load_module(playlist[playlist_current].path);
        play_current();
    }

    void play_prev() {
        if (playlist_count == 0) return;
        playlist_current--;
        if (playlist_current < 0) playlist_current = playlist_count - 1;
        load_module(playlist[playlist_current].path);
        play_current();
    }

    void add_to_playlist(const char* path, const char* name) {
        if (playlist_count >= MAX_PLAYLIST) { set_status("Playlist full!"); return; }
        neo_strcpy(playlist[playlist_count].path, path);
        neo_strncpy(playlist[playlist_count].name, name, 63);
        playlist[playlist_count].name[63] = '\0';
        playlist[playlist_count].valid = true;
        playlist_count++;
        char msg[64];
        ksprintf(msg, sizeof(msg), "Added to playlist: %s", name);
        set_status(msg);
    }

    // ---- Rendering ----

    void render_browser() {
        neo::display::clear();

        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoPlayer | %s | %d files | Playlist: %d ",
                 current_path, num_files, playlist_count);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // File list
        neo::display::set_color(0, 6);
        neo::display::set_cursor(0, 1);
        neo::display::puts("   Name                              Size      ");
        for (int i = 48; i < screen_w; i++) {
            neo::display::set_cursor(i, 1);
            neo::display::putchar(' ');
        }

        int visible = screen_h - 4;
        if (file_selected < file_scroll) file_scroll = file_selected;
        if (file_selected >= file_scroll + visible) file_scroll = file_selected - visible + 1;

        for (int i = 0; i < visible && file_scroll + i < num_files; i++) {
            int idx = file_scroll + i;
            int sy = 2 + i;
            FileEntry& f = files[idx];

            if (idx == file_selected) neo::display::set_color(0, 7);
            else if (f.is_dir) neo::display::set_color(3, 0);
            else if (is_mod_file(f.name)) neo::display::set_color(2, 0);
            else neo::display::set_color(7, 0);

            neo::display::set_cursor(0, sy);
            char line[80];
            ksprintf(line, sizeof(line), " %-34s %8d", f.name, f.size);
            neo::display::puts(line);
        }

        // Status
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        neo::display::puts(" Enter:Play A:Add to playlist P:Playlist V:Player Esc:Quit ");
        for (int i = 58; i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 1);
            neo::display::putchar(' ');
        }
    }

    void render_player() {
        neo::display::clear();

        // Header
        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        const char* state_str = state == STATE_PLAYING ? "PLAY" :
                                state == STATE_PAUSED ? "PAUSE" : "STOP";
        ksprintf(hdr, sizeof(hdr), " NeoPlayer | %s | [%s] BPM:%d Vol:%d%% %s ",
                 module.loaded ? module.title : "(no module)",
                 state_str, module.bpm, master_volume,
                 loop_mode ? "LOOP" : "");
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        // Module info
        neo::display::set_color(7, 0);
        neo::display::set_cursor(2, 2);
        if (module.loaded) {
            char info[80];
            ksprintf(info, sizeof(info), "Patterns: %d | Samples: %d | Order: %d/%d | Row: %d/%d",
                     module.num_patterns, module.num_samples,
                     module.current_order + 1, module.song_length,
                     module.current_row, PATTERN_ROWS);
            neo::display::puts(info);
        }

        // VU Meters
        int vu_y = 4;
        int vu_x_start = 4;
        int meter_width = 10;
        int spacing = (screen_w - 8) / NUM_CHANNELS;

        for (int c = 0; c < NUM_CHANNELS; c++) {
            int mx = vu_x_start + c * spacing;

            // Channel header
            neo::display::set_color(6, 0);
            neo::display::set_cursor(mx, vu_y);
            char chdr[16];
            ksprintf(chdr, sizeof(chdr), "  Ch %d  ", c + 1);
            neo::display::puts(chdr);

            // Note display
            neo::display::set_cursor(mx, vu_y + 1);
            char nbuf[8];
            format_note(channels[c].note, nbuf);
            neo::display::set_fg(2);
            neo::display::puts(nbuf);
            neo::display::set_fg(7);
            ksprintf(nbuf, sizeof(nbuf), " I%02d", channels[c].instrument);
            neo::display::puts(nbuf);

            // VU meter (vertical)
            for (int y = 0; y < VU_HEIGHT; y++) {
                int level = VU_HEIGHT - 1 - y;
                neo::display::set_cursor(mx, vu_y + 3 + y);
                if (level < channels[c].vu_level) {
                    if (level >= VU_HEIGHT - 3) neo::display::set_fg(1); // red peak
                    else if (level >= VU_HEIGHT / 2) neo::display::set_fg(3); // yellow
                    else neo::display::set_fg(2); // green
                    neo::display::puts("========");
                } else {
                    neo::display::set_fg(1);
                    neo::display::puts("--------");
                }
            }

            // Volume
            neo::display::set_fg(7);
            neo::display::set_cursor(mx, vu_y + 3 + VU_HEIGHT);
            ksprintf(chdr, sizeof(chdr), " V:%2d  ", channels[c].volume);
            neo::display::puts(chdr);
        }

        // Waveform display
        int wave_y = vu_y + VU_HEIGHT + 6;
        if (wave_y + 4 < screen_h - 2) {
            neo::display::set_color(6, 0);
            neo::display::set_cursor(2, wave_y);
            neo::display::puts("Waveform:");

            int wave_h = 3;
            int wx = 2;
            int wave_w = screen_w - 4;
            if (wave_w > WAVE_WIDTH) wave_w = WAVE_WIDTH;

            for (int wy = 0; wy < wave_h; wy++) {
                neo::display::set_cursor(wx, wave_y + 1 + wy);
                for (int x = 0; x < wave_w; x++) {
                    int mid = wave_h / 2;
                    bool has_signal = false;
                    for (int c = 0; c < NUM_CHANNELS; c++) {
                        if (channels[c].active && channels[c].vu_level > 0) {
                            // Simple sine-ish wave visualization
                            int phase = (x * channels[c].frequency / 40 + channels[c].wave_phase) % 24;
                            int wave_val;
                            if (phase < 6) wave_val = phase;
                            else if (phase < 12) wave_val = 12 - phase;
                            else if (phase < 18) wave_val = 12 - phase;
                            else wave_val = phase - 24;
                            // Map to display row
                            int mapped = mid + (wave_val * wave_h) / 12;
                            if (mapped == wy) { has_signal = true; break; }
                        }
                    }
                    if (has_signal) {
                        neo::display::set_fg(2);
                        neo::display::putchar('~');
                    } else if (wy == mid) {
                        neo::display::set_fg(1);
                        neo::display::putchar('-');
                    } else {
                        neo::display::putchar(' ');
                    }
                }
            }
        }

        // Controls
        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 2);
        neo::display::puts(" Space:Play/Pause S:Stop </> Prev/Next +/-:Vol L:Loop B:Browser P:Playlist ");
        for (int i = 74; i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 2);
            neo::display::putchar(' ');
        }

        if (status_timer > 0) {
            neo::display::set_color(0, 5);
            neo::display::set_cursor(0, screen_h - 1);
            neo::display::puts(status_msg);
            for (int i = neo_strlen(status_msg); i < screen_w; i++) {
                neo::display::set_cursor(i, screen_h - 1);
                neo::display::putchar(' ');
            }
        }
    }

    void render_playlist() {
        neo::display::clear();

        neo::display::set_color(0, 3);
        neo::display::set_cursor(0, 0);
        char hdr[80];
        ksprintf(hdr, sizeof(hdr), " NeoPlayer Playlist | %d tracks | Current: %d ",
                 playlist_count, playlist_current + 1);
        neo::display::puts(hdr);
        for (int i = neo_strlen(hdr); i < screen_w; i++) {
            neo::display::set_cursor(i, 0);
            neo::display::putchar(' ');
        }

        if (playlist_count == 0) {
            neo::display::set_color(7, 0);
            neo::display::set_cursor(screen_w / 2 - 10, screen_h / 2);
            neo::display::puts("Playlist is empty!");
            neo::display::set_cursor(screen_w / 2 - 15, screen_h / 2 + 2);
            neo::display::puts("Use browser to add tracks (A key)");
        } else {
            neo::display::set_color(0, 6);
            neo::display::set_cursor(0, 2);
            neo::display::puts("  # | Track Name                              ");
            for (int i = 46; i < screen_w; i++) {
                neo::display::set_cursor(i, 2);
                neo::display::putchar(' ');
            }

            for (int i = 0; i < playlist_count && i + 3 < screen_h - 2; i++) {
                int sy = 3 + i;
                if (i == playlist_current && state == STATE_PLAYING)
                    neo::display::set_color(0, 2);
                else if (i == playlist_current)
                    neo::display::set_color(0, 7);
                else
                    neo::display::set_color(7, 0);

                neo::display::set_cursor(0, sy);
                char line[80];
                ksprintf(line, sizeof(line), " %2d | %-40s", i + 1, playlist[i].name);
                neo::display::puts(line);
                if (i == playlist_current && state == STATE_PLAYING) {
                    neo::display::puts(" >> ");
                }
            }
        }

        neo::display::set_color(0, 2);
        neo::display::set_cursor(0, screen_h - 1);
        neo::display::puts(" B:Browser V:Player D:Delete Esc:Back ");
        for (int i = 38; i < screen_w; i++) {
            neo::display::set_cursor(i, screen_h - 1);
            neo::display::putchar(' ');
        }
    }

    void open_selected_file() {
        if (file_selected < 0 || file_selected >= num_files) return;
        FileEntry& f = files[file_selected];

        if (f.is_dir) {
            if (neo_strcmp(f.name, "..") == 0) {
                int len = neo_strlen(current_path);
                while (len > 0 && current_path[len-1] != '/' && current_path[len-1] != ':') len--;
                current_path[len] = '\0';
            } else {
                neo_strcat(current_path, f.name);
                neo_strcat(current_path, "/");
            }
            scan_directory();
        } else {
            char full_path[INODE_SIZE];
            ksprintf(full_path, sizeof(full_path), "%s%s", current_path, f.name);
            if (load_module(full_path)) {
                play_current();
                view = VIEW_PLAYER;
            }
        }
    }

    void handle_browser_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (sc == 0x4C && file_selected > 0) file_selected--;
        else if (sc == 0x4D && file_selected < num_files - 1) file_selected++;
        else if (sc == 0x44 || ch == '\n' || ch == '\r') open_selected_file();
        else if (ch == 'a' || ch == 'A') {
            if (file_selected >= 0 && file_selected < num_files && !files[file_selected].is_dir) {
                char full_path[INODE_SIZE];
                ksprintf(full_path, sizeof(full_path), "%s%s", current_path, files[file_selected].name);
                add_to_playlist(full_path, files[file_selected].name);
            }
        }
        else if (ch == 'v' || ch == 'V') view = VIEW_PLAYER;
        else if (ch == 'p' || ch == 'P') view = VIEW_PLAYLIST;
        else if (sc == 0x45 || ch == 'q' || ch == 'Q') running = false;
    }

    void handle_player_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (ch == ' ') {
            if (state == STATE_STOPPED && module.loaded) play_current();
            else toggle_pause();
        }
        else if (ch == 's' || ch == 'S') stop_playback();
        else if (ch == ',' || ch == '<') play_prev();
        else if (ch == '.' || ch == '>') play_next();
        else if (ch == '+' || ch == '=') { if (master_volume < 100) master_volume += 5; }
        else if (ch == '-') { if (master_volume > 0) master_volume -= 5; }
        else if (ch == 'l' || ch == 'L') { loop_mode = !loop_mode; set_status(loop_mode ? "Loop ON" : "Loop OFF"); }
        else if (ch == 'b' || ch == 'B') view = VIEW_BROWSER;
        else if (ch == 'p' || ch == 'P') view = VIEW_PLAYLIST;
        else if (sc == 0x45 || ch == 'q' || ch == 'Q') running = false;
    }

    void handle_playlist_key(unsigned char sc) {
        char ch = neo::keyboard::translate(sc, false);
        if (ch == 'b' || ch == 'B') view = VIEW_BROWSER;
        else if (ch == 'v' || ch == 'V') view = VIEW_PLAYER;
        else if (ch == 'd' || ch == 'D' && playlist_count > 0) {
            // Delete current
            for (int i = playlist_current; i < playlist_count - 1; i++)
                playlist[i] = playlist[i + 1];
            playlist_count--;
            if (playlist_current >= playlist_count) playlist_current = playlist_count - 1;
        }
        else if (sc == 0x45) view = VIEW_PLAYER;
    }

    void run() {
        init();
        scan_directory();

        while (running) {
            switch (view) {
                case VIEW_BROWSER: render_browser(); break;
                case VIEW_PLAYER: render_player(); break;
                case VIEW_PLAYLIST: render_playlist(); break;
            }

            if (status_timer > 0) status_timer--;
            if (status_timer == 0) status_msg[0] = '\0';

            // Playback tick
            if (state == STATE_PLAYING) {
                play_step();
                decay_vu();
            }

            int wait = 0;
            while (!neo::keyboard::key_available() && wait < 3) {
                neo::timer::delay_ms(16);
                wait++;
                if (state == STATE_PLAYING) { play_step(); decay_vu(); }
            }

            if (neo::keyboard::key_available()) {
                unsigned char sc = neo::keyboard::read_scancode();
                if (sc & 0x80) continue;
                switch (view) {
                    case VIEW_BROWSER: handle_browser_key(sc); break;
                    case VIEW_PLAYER: handle_player_key(sc); break;
                    case VIEW_PLAYLIST: handle_playlist_key(sc); break;
                }
            }
        }

        stop_playback();
        neo::display::clear();
        neo::display::set_color(7, 0);
        kprintf("NeoPlayer: Goodbye.\n");
    }
};

} // namespace neoplayer

extern "C" void app_main(int argc, char** argv) {
    neoplayer::PlayerApp app;
    app.run();
}
