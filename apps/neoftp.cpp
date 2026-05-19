#include "../include/neobench.h"
#include "../lib/string.h"

// NeoFTP - FTP Client for NeoBench
// Split-pane local/remote view, upload/download, bookmarks, ASCII/binary

namespace neoftp {

// --- Connection ---
struct FtpConnection {
    char host[128];
    int port;
    char username[64];
    char password[64];
    char remote_cwd[INODE_SIZE];
    bool connected;
    bool binary_mode;
};

static FtpConnection conn;

// --- File Entry ---
struct FileEntry {
    char name[INODE_SIZE];
    unsigned int size;
    int type; // 0=file, 1=dir
    bool selected;
};

static const int MAX_FILES = 64;
static FileEntry local_files[MAX_FILES];
static int local_count = 0;
static int local_cursor = 0;
static char local_cwd[INODE_SIZE];

static FileEntry remote_files[MAX_FILES];
static int remote_count = 0;
static int remote_cursor = 0;

static int active_pane = 0; // 0=local, 1=remote

// --- Server Bookmarks ---
struct ServerBookmark {
    char name[64];
    char host[128];
    int port;
    char username[64];
    char password[64];
    bool valid;
};

static const int MAX_BOOKMARKS = 8;
static ServerBookmark bookmarks[MAX_BOOKMARKS];
static int bookmark_count = 0;

// --- Transfer queue ---
struct TransferItem {
    char local_path[INODE_SIZE];
    char remote_path[INODE_SIZE];
    unsigned int size;
    bool upload; // true=upload, false=download
    bool active;
};

// --- Simulated remote filesystem ---
struct RemoteDir {
    const char* path;
    const char* files; // "name1,size1,type1;name2,size2,type2;..."
};

static const RemoteDir sim_dirs[] = {
    {"/", "pub,0,1;incoming,0,1;welcome.txt,1234,0;readme.txt,567,0;"},
    {"/pub", "aminet,0,1;demos,0,1;games,0,1;utils,0,1;index.txt,890,0;"},
    {"/pub/aminet", "DirOpus582.lha,234567,0;MagicWB20.lha,123456,0;Shapeshifter36.lha,345678,0;"},
    {"/pub/demos", "StateOfTheArt.lha,456789,0;9fingers.lha,234567,0;Enigma.lha,567890,0;"},
    {"/pub/games", "Turrican2.lha,789012,0;Speedball2.lha,345678,0;Lemmings.lha,456789,0;"},
    {"/pub/utils", "LhA213.lha,45678,0;PowerPacker.lha,34567,0;ReqTools.lha,23456,0;"},
    {"/incoming", "upload_here.txt,123,0;"},
};
static const int NUM_SIM_DIRS = 7;

void parse_sim_dir(const char* files_str, FileEntry* entries, int& count) {
    count = 0;
    if (!files_str) return;

    const char* p = files_str;
    while (*p && count < MAX_FILES) {
        FileEntry& e = entries[count];
        e.selected = false;

        // Name
        int ni = 0;
        while (*p && *p != ',' && ni < 255) e.name[ni++] = *p++;
        e.name[ni] = 0;
        if (*p == ',') p++;

        // Size
        e.size = 0;
        while (*p >= '0' && *p <= '9') { e.size = e.size * 10 + (*p - '0'); p++; }
        if (*p == ',') p++;

        // Type
        e.type = 0;
        if (*p >= '0' && *p <= '9') e.type = *p - '0';
        p++;
        if (*p == ';') p++;

        if (e.name[0]) count++;
    }
}

void load_remote_dir() {
    remote_count = 0;
    // Add parent dir entry
    if (neo_strcmp(conn.remote_cwd, "/") != 0) {
        neo_strcpy(remote_files[0].name, "..");
        remote_files[0].size = 0;
        remote_files[0].type = 1;
        remote_files[0].selected = false;
        remote_count = 1;
    }

    for (int i = 0; i < NUM_SIM_DIRS; i++) {
        if (neo_strcmp(sim_dirs[i].path, conn.remote_cwd) == 0) {
            FileEntry temp[MAX_FILES];
            int temp_count = 0;
            parse_sim_dir(sim_dirs[i].files, temp, temp_count);
            for (int j = 0; j < temp_count && remote_count < MAX_FILES; j++) {
                remote_files[remote_count] = temp[j];
                remote_count++;
            }
            break;
        }
    }
}

void load_local_dir() {
    local_count = 0;
    neo::filesystem::DirEntry entries[MAX_FILES];
    int n = neo::filesystem::readdir(local_cwd, entries, MAX_FILES);

    // Add parent dir
    if (neo_strcmp(local_cwd, "/") != 0) {
        neo_strcpy(local_files[0].name, "..");
        local_files[0].size = 0;
        local_files[0].type = 1;
        local_files[0].selected = false;
        local_count = 1;
    }

    for (int i = 0; i < n && local_count < MAX_FILES; i++) {
        neo_strncpy(local_files[local_count].name, entries[i].name, 255);
        local_files[local_count].size = entries[i].size;
        local_files[local_count].type = entries[i].type;
        local_files[local_count].selected = false;
        local_count++;
    }
}

// --- FTP Protocol Simulation ---
void ftp_connect(const char* host, int port, const char* user, const char* pass) {
    neo::display::printf("Connecting to %s:%d...\n", host, port);
    neo::timer::delay_ms(300);
    neo::display::puts("220 Welcome to NeoFTP Server\n");
    neo::timer::delay_ms(100);

    neo::display::printf("USER %s\n", user);
    neo::display::puts("331 Password required\n");
    neo::timer::delay_ms(100);

    neo::display::puts("PASS ****\n");
    neo::display::puts("230 User logged in\n");
    neo::timer::delay_ms(100);

    neo::display::puts("PWD\n");
    neo::display::puts("257 \"/\" is current directory\n");
    neo::timer::delay_ms(100);

    neo_strncpy(conn.host, host, 127);
    conn.port = port;
    neo_strncpy(conn.username, user, 63);
    neo_strncpy(conn.password, pass, 63);
    neo_strcpy(conn.remote_cwd, "/");
    conn.connected = true;
    conn.binary_mode = true;

    load_remote_dir();
    neo::display::puts("\nConnected successfully.\n");
    neo::timer::delay_ms(500);
}

void ftp_cwd(const char* dir) {
    if (neo_strcmp(dir, "..") == 0) {
        // Go up
        char* last = (char*)neo_strchr(conn.remote_cwd + 1, '/');
        // Simple: find last /
        int len = neo_strlen(conn.remote_cwd);
        if (len > 1) {
            for (int i = len - 1; i > 0; i--) {
                if (conn.remote_cwd[i] == '/') {
                    conn.remote_cwd[i] = 0;
                    break;
                }
            }
            if (conn.remote_cwd[0] == 0) neo_strcpy(conn.remote_cwd, "/");
        }
    } else {
        if (neo_strcmp(conn.remote_cwd, "/") != 0)
            neo_strcat(conn.remote_cwd, "/");
        neo_strcat(conn.remote_cwd, dir);
    }
    load_remote_dir();
}

void ftp_disconnect() {
    if (!conn.connected) return;
    neo::display::puts("QUIT\n");
    neo::display::puts("221 Goodbye\n");
    conn.connected = false;
}

// --- Progress bar ---
void draw_progress(int x, int y, int width, unsigned int current, unsigned int total) {
    neo::display::set_cursor(x, y);
    neo::display::putchar('[');
    int filled = 0;
    if (total > 0) filled = (int)((unsigned long long)current * (width - 2) / total);
    for (int i = 0; i < width - 2; i++) {
        neo::display::putchar(i < filled ? '#' : '.');
    }
    neo::display::putchar(']');
    if (total > 0) {
        int pct = (int)((unsigned long long)current * 100 / total);
        neo::display::printf(" %d%%", pct);
    }
}

void simulate_transfer(const char* filename, unsigned int size, bool upload) {
    int w = neo::display::get_width();
    int h = neo::display::get_height();

    neo::display::set_cursor(0, h - 4);
    neo::display::set_color(14, 0);
    neo::display::printf("%s: %s (%d bytes)\n",
                         upload ? "Uploading" : "Downloading", filename, size);
    neo::display::set_color(7, 0);

    unsigned int transferred = 0;
    unsigned int chunk = size / 20;
    if (chunk < 100) chunk = 100;

    while (transferred < size) {
        transferred += chunk;
        if (transferred > size) transferred = size;
        draw_progress(0, h - 3, w - 10, transferred, size);
        neo::timer::delay_ms(100);

        if (neo::keyboard::key_available()) {
            unsigned char sc = neo::keyboard::read_scancode();
            if (sc == 0x01) { // ESC
                neo::display::puts("\nTransfer cancelled.\n");
                return;
            }
        }
    }

    neo::display::set_cursor(0, h - 2);
    neo::display::set_color(10, 0);
    neo::display::printf("Transfer complete: %d bytes\n", size);
    neo::display::set_color(7, 0);
    neo::timer::delay_ms(500);
}

// --- Size formatting ---
void format_size(char* buf, int maxlen, unsigned int size) {
    if (size >= 1048576) {
        ksprintf(buf, maxlen, "%dM", size / 1048576);
    } else if (size >= 1024) {
        ksprintf(buf, maxlen, "%dK", size / 1024);
    } else {
        ksprintf(buf, maxlen, "%d", size);
    }
}

// --- UI Drawing ---
void draw_ui() {
    neo::display::clear();
    int w = neo::display::get_width();
    int h = neo::display::get_height();
    int half = w / 2;

    // Title bar
    neo::display::set_color(15, 1);
    neo::display::puts(" NeoFTP v1.0 ");
    if (conn.connected) {
        neo::display::set_color(10, 1);
        neo::display::printf("| Connected: %s ", conn.host);
    } else {
        neo::display::set_color(12, 1);
        neo::display::puts("| Disconnected ");
    }
    neo::display::set_color(7, 1);
    neo::display::printf("| Mode: %s ", conn.binary_mode ? "BIN" : "ASC");
    int used = 30 + (conn.connected ? neo_strlen(conn.host) : 0);
    for (int i = used; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);

    // Local pane header
    neo::display::set_cursor(0, 1);
    if (active_pane == 0) neo::display::set_color(14, 0);
    else neo::display::set_color(8, 0);
    neo::display::puts("Local: ");
    neo::display::puts(local_cwd);
    neo::display::set_color(7, 0);

    // Remote pane header
    neo::display::set_cursor(half, 1);
    if (active_pane == 1) neo::display::set_color(14, 0);
    else neo::display::set_color(8, 0);
    neo::display::puts("Remote: ");
    neo::display::puts(conn.connected ? conn.remote_cwd : "(not connected)");
    neo::display::set_color(7, 0);

    // Separator
    neo::display::set_cursor(0, 2);
    for (int i = 0; i < w; i++) {
        neo::display::putchar(i == half - 1 ? '|' : '-');
    }

    // Local files
    int max_display = h - 6;
    for (int i = 0; i < max_display && i < local_count; i++) {
        neo::display::set_cursor(0, 3 + i);
        FileEntry& f = local_files[i];
        if (i == local_cursor && active_pane == 0) {
            neo::display::set_color(0, 7); // Highlighted
        } else if (f.selected) {
            neo::display::set_color(14, 0);
        } else if (f.type == 1) {
            neo::display::set_fg(11); // Cyan for dirs
        } else {
            neo::display::set_fg(7);
        }

        char szbuf[16];
        if (f.type == 1) neo_strcpy(szbuf, "<DIR>");
        else format_size(szbuf, 16, f.size);

        // Truncate name to fit
        char dispname[32];
        int maxname = half - 12;
        if (maxname > 31) maxname = 31;
        neo_strncpy(dispname, f.name, maxname);
        dispname[maxname] = 0;

        neo::display::printf(" %-*s %8s", maxname, dispname, szbuf);
        neo::display::set_color(7, 0);
    }

    // Vertical separator
    for (int i = 3; i < h - 3; i++) {
        neo::display::set_cursor(half - 1, i);
        neo::display::set_fg(8);
        neo::display::putchar('|');
        neo::display::set_fg(7);
    }

    // Remote files
    for (int i = 0; i < max_display && i < remote_count; i++) {
        neo::display::set_cursor(half, 3 + i);
        FileEntry& f = remote_files[i];
        if (i == remote_cursor && active_pane == 1) {
            neo::display::set_color(0, 7);
        } else if (f.selected) {
            neo::display::set_color(14, 0);
        } else if (f.type == 1) {
            neo::display::set_fg(11);
        } else {
            neo::display::set_fg(7);
        }

        char szbuf[16];
        if (f.type == 1) neo_strcpy(szbuf, "<DIR>");
        else format_size(szbuf, 16, f.size);

        char dispname[32];
        int maxname = half - 12;
        if (maxname > 31) maxname = 31;
        neo_strncpy(dispname, f.name, maxname);
        dispname[maxname] = 0;

        neo::display::printf(" %-*s %8s", maxname, dispname, szbuf);
        neo::display::set_color(7, 0);
    }

    // Status bar
    neo::display::set_cursor(0, h - 2);
    for (int i = 0; i < w; i++) neo::display::putchar('-');

    neo::display::set_cursor(0, h - 1);
    neo::display::set_color(0, 3);
    neo::display::puts(" Tab:pane Up/Dn:nav Enter:open F5:copy F7:mkdir F8:del C:conn B:bkmk Q:quit ");
    for (int i = 75; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

void show_bookmarks() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+============================+\n");
    neo::display::puts("|     Server Bookmarks       |\n");
    neo::display::puts("+============================+\n");
    neo::display::set_color(7, 0);

    for (int i = 0; i < bookmark_count; i++) {
        if (!bookmarks[i].valid) continue;
        neo::display::printf("  %d. %s (%s@%s:%d)\n", i + 1,
                             bookmarks[i].name, bookmarks[i].username,
                             bookmarks[i].host, bookmarks[i].port);
    }
    if (bookmark_count == 0) neo::display::puts("  (No bookmarks)\n");

    neo::display::puts("\n[number] to connect, [a]dd, [b]ack: ");
    char input[64];
    neo::console::getline(input, sizeof(input), "");
    if (input[0] == 'a' || input[0] == 'A') {
        if (bookmark_count < MAX_BOOKMARKS) {
            ServerBookmark& bm = bookmarks[bookmark_count];
            neo::console::getline(bm.name, 64, "Name: ");
            neo::console::getline(bm.host, 128, "Host: ");
            char portstr[8];
            neo::console::getline(portstr, 8, "Port [21]: ");
            bm.port = 21;
            if (portstr[0]) {
                bm.port = 0;
                for (int i = 0; portstr[i] >= '0' && portstr[i] <= '9'; i++)
                    bm.port = bm.port * 10 + (portstr[i] - '0');
            }
            neo::console::getline(bm.username, 64, "Username: ");
            neo::console::getline(bm.password, 64, "Password: ");
            bm.valid = true;
            bookmark_count++;
        }
    } else if (input[0] >= '1' && input[0] <= '9') {
        int sel = input[0] - '0';
        if (sel >= 1 && sel <= bookmark_count && bookmarks[sel-1].valid) {
            ftp_connect(bookmarks[sel-1].host, bookmarks[sel-1].port,
                       bookmarks[sel-1].username, bookmarks[sel-1].password);
        }
    }
}

void connect_dialog() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+============================+\n");
    neo::display::puts("|     Connect to Server      |\n");
    neo::display::puts("+============================+\n");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');

    char host[128], portstr[8], user[64], pass[64];
    neo::console::getline(host, sizeof(host), "Host: ");
    if (host[0] == 0) return;
    neo::console::getline(portstr, sizeof(portstr), "Port [21]: ");
    int port = 21;
    if (portstr[0]) {
        port = 0;
        for (int i = 0; portstr[i] >= '0' && portstr[i] <= '9'; i++)
            port = port * 10 + (portstr[i] - '0');
    }
    neo::console::getline(user, sizeof(user), "Username [anonymous]: ");
    if (user[0] == 0) neo_strcpy(user, "anonymous");
    neo::console::getline(pass, sizeof(pass), "Password: ");
    if (pass[0] == 0) neo_strcpy(pass, "user@neobench.local");

    ftp_connect(host, port, user, pass);
}

} // namespace neoftp

extern "C" void app_main(int argc, char** argv) {
    using namespace neoftp;

    neo_memset(&conn, 0, sizeof(conn));
    neo_memset(local_files, 0, sizeof(local_files));
    neo_memset(remote_files, 0, sizeof(remote_files));
    neo_memset(bookmarks, 0, sizeof(bookmarks));
    local_count = 0;
    remote_count = 0;
    local_cursor = 0;
    remote_cursor = 0;
    active_pane = 0;
    bookmark_count = 0;

    // Default bookmark
    neo_strcpy(bookmarks[0].name, "Aminet Mirror");
    neo_strcpy(bookmarks[0].host, "ftp.aminet.net");
    bookmarks[0].port = 21;
    neo_strcpy(bookmarks[0].username, "anonymous");
    neo_strcpy(bookmarks[0].password, "user@neobench.local");
    bookmarks[0].valid = true;
    bookmark_count = 1;

    // Init local directory
    neo_strcpy(local_cwd, "/");
    load_local_dir();

    draw_ui();

    bool running = true;
    while (running) {
        if (!neo::keyboard::key_available()) { neo::proc::yield(); continue; }

        unsigned char sc = neo::keyboard::read_scancode();
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);
        bool redraw = true;

        // Handle scancodes for special keys
        if (sc == 0x4C) { // Up
            if (active_pane == 0 && local_cursor > 0) local_cursor--;
            else if (active_pane == 1 && remote_cursor > 0) remote_cursor--;
        }
        else if (sc == 0x4D) { // Down
            if (active_pane == 0 && local_cursor < local_count - 1) local_cursor++;
            else if (active_pane == 1 && remote_cursor < remote_count - 1) remote_cursor++;
        }
        else if (sc == 0x44 || ch == '\n') { // Enter
            if (active_pane == 0 && local_cursor < local_count) {
                FileEntry& f = local_files[local_cursor];
                if (f.type == 1) {
                    if (neo_strcmp(f.name, "..") == 0) {
                        // Go up
                        int len = neo_strlen(local_cwd);
                        if (len > 1) {
                            for (int i = len - 1; i > 0; i--) {
                                if (local_cwd[i] == '/') { local_cwd[i] = 0; break; }
                            }
                            if (local_cwd[0] == 0) neo_strcpy(local_cwd, "/");
                        }
                    } else {
                        if (neo_strcmp(local_cwd, "/") != 0) neo_strcat(local_cwd, "/");
                        neo_strcat(local_cwd, f.name);
                    }
                    local_cursor = 0;
                    load_local_dir();
                }
            } else if (active_pane == 1 && remote_cursor < remote_count && conn.connected) {
                FileEntry& f = remote_files[remote_cursor];
                if (f.type == 1) {
                    ftp_cwd(f.name);
                    remote_cursor = 0;
                }
            }
        }
        else if (sc == 0x42) { // Tab
            active_pane = 1 - active_pane;
        }
        else if (sc == 0x54) { // F5 - copy/transfer
            if (active_pane == 0 && conn.connected && local_cursor < local_count) {
                FileEntry& f = local_files[local_cursor];
                if (f.type == 0) simulate_transfer(f.name, f.size, true);
            } else if (active_pane == 1 && conn.connected && remote_cursor < remote_count) {
                FileEntry& f = remote_files[remote_cursor];
                if (f.type == 0) simulate_transfer(f.name, f.size, false);
            }
        }
        else if (sc == 0x56) { // F7 - mkdir
            char dirname[64];
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 3);
            neo::display::clear_eol();
            neo::console::getline(dirname, sizeof(dirname), "Create directory: ");
            if (dirname[0]) {
                neo::display::printf("MKD %s\n257 Directory created\n", dirname);
                neo::timer::delay_ms(300);
                if (active_pane == 1 && conn.connected) load_remote_dir();
                else load_local_dir();
            }
        }
        else if (sc == 0x57) { // F8 - delete
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 3);
            neo::display::set_color(12, 0);
            if (active_pane == 0 && local_cursor < local_count) {
                neo::display::printf("Delete %s? [y/n]", local_files[local_cursor].name);
            } else if (active_pane == 1 && remote_cursor < remote_count) {
                neo::display::printf("Delete %s? [y/n]", remote_files[remote_cursor].name);
            }
            neo::display::set_color(7, 0);
            while (!neo::keyboard::key_available()) neo::proc::yield();
            unsigned char confirm_sc = neo::keyboard::read_scancode();
            char confirm = neo::keyboard::translate(confirm_sc, false);
            if (confirm == 'y') {
                neo::display::puts(" Deleted.\n");
                neo::timer::delay_ms(300);
            }
        }
        else if (ch == 'c' || ch == 'C') { connect_dialog(); }
        else if (ch == 'b' || ch == 'B') { show_bookmarks(); }
        else if (ch == 't' || ch == 'T') {
            conn.binary_mode = !conn.binary_mode;
            neo::display::printf("TYPE %s\n200 OK\n", conn.binary_mode ? "I" : "A");
            neo::timer::delay_ms(300);
        }
        else if (ch == 'q' || ch == 'Q') {
            running = false;
            redraw = false;
        }
        else {
            redraw = false;
        }

        if (redraw) draw_ui();
    }

    ftp_disconnect();
    neo::display::clear();
    neo::display::set_color(7, 0);
    neo::display::puts("NeoFTP session ended.\n");
}
