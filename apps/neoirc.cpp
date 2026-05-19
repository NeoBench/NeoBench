#include "../include/neobench.h"
#include "../lib/string.h"

// NeoIRC - IRC Chat Client for NeoBench
// Multi-channel, user list, nick colors, IRC protocol simulation

namespace neoirc {

// --- Config ---
struct ServerConfig {
    char host[128];
    int port;
    char nick[32];
    char username[32];
    char realname[64];
    bool connected;
};

static ServerConfig server;

// --- Channel ---
struct ChannelMessage {
    char nick[32];
    char text[INODE_SIZE];
    unsigned int timestamp;
    int type; // 0=msg, 1=action, 2=join, 3=part, 4=system, 5=notice
};

static const int MAX_MSGS = 64;
static const int MAX_USERS = 32;
static const int MAX_CHANNELS = 8;

struct Channel {
    char name[64];
    char topic[INODE_SIZE];
    ChannelMessage messages[MAX_MSGS];
    int msg_count;
    int msg_scroll;
    char users[MAX_USERS][32];
    int user_count;
    bool active;
    bool auto_rejoin;
};

static Channel channels[MAX_CHANNELS];
static int channel_count = 0;
static int active_channel = 0;

// --- Nick Color ---
int nick_color(const char* nick) {
    unsigned int hash = 0;
    for (int i = 0; nick[i]; i++) hash = hash * 31 + nick[i];
    int colors[] = {9, 10, 11, 12, 13, 14, 3, 6};
    return colors[hash % 8];
}

// --- Timestamp ---
void get_timestamp(char* buf, int maxlen) {
    unsigned int secs = neo::timer::get_uptime_seconds();
    int h = (secs / 3600) % 24;
    int m = (secs / 60) % 60;
    ksprintf(buf, maxlen, "%02d:%02d", h, m);
}

// --- Channel management ---
Channel* find_channel(const char* name) {
    for (int i = 0; i < channel_count; i++) {
        if (channels[i].active && neo_strcmp(channels[i].name, name) == 0)
            return &channels[i];
    }
    return nullptr;
}

int find_channel_idx(const char* name) {
    for (int i = 0; i < channel_count; i++) {
        if (channels[i].active && neo_strcmp(channels[i].name, name) == 0)
            return i;
    }
    return -1;
}

Channel* create_channel(const char* name) {
    if (channel_count >= MAX_CHANNELS) return nullptr;
    Channel& ch = channels[channel_count];
    neo_memset(&ch, 0, sizeof(Channel));
    neo_strncpy(ch.name, name, 63);
    ch.active = true;
    ch.auto_rejoin = true;
    ch.msg_count = 0;
    ch.msg_scroll = 0;
    ch.user_count = 0;
    channel_count++;
    return &ch;
}

void add_message(Channel* ch, const char* nick, const char* text, int type) {
    if (!ch || ch->msg_count >= MAX_MSGS) {
        // Shift messages
        if (ch && ch->msg_count >= MAX_MSGS) {
            for (int i = 0; i < MAX_MSGS - 1; i++) {
                ch->messages[i] = ch->messages[i + 1];
            }
            ch->msg_count = MAX_MSGS - 1;
        } else return;
    }
    ChannelMessage& m = ch->messages[ch->msg_count];
    neo_strncpy(m.nick, nick, 31); m.nick[31] = 0;
    neo_strncpy(m.text, text, 255); m.text[255] = 0;
    m.timestamp = neo::timer::get_uptime_seconds();
    m.type = type;
    ch->msg_count++;
}

void add_user(Channel* ch, const char* nick) {
    if (!ch) return;
    // Check duplicate
    for (int i = 0; i < ch->user_count; i++) {
        if (neo_strcmp(ch->users[i], nick) == 0) return;
    }
    if (ch->user_count < MAX_USERS) {
        neo_strncpy(ch->users[ch->user_count], nick, 31);
        ch->users[ch->user_count][31] = 0;
        ch->user_count++;
    }
}

void remove_user(Channel* ch, const char* nick) {
    if (!ch) return;
    for (int i = 0; i < ch->user_count; i++) {
        if (neo_strcmp(ch->users[i], nick) == 0) {
            for (int j = i; j < ch->user_count - 1; j++) {
                neo_strcpy(ch->users[j], ch->users[j + 1]);
            }
            ch->user_count--;
            return;
        }
    }
}

// --- Simulate IRC activity ---
static unsigned int sim_rng = 0;
static const char* sim_nicks[] = {
    "Turrican", "Flashback", "CopperBob", "BlitterQueen",
    "DemoStar", "ChipTune", "AmigaFan", "RetroHero"
};
static const char* sim_msgs[] = {
    "Hey everyone, what's up?",
    "Anyone working on any cool demos?",
    "Just got a new A1200 expansion board!",
    "The copper effects in that demo were amazing",
    "Does anyone have the latest AGA toolkit?",
    "brb, swapping floppies :)",
    "Check out the new release on Aminet",
    "Who's coming to the next demoparty?",
    "My blitter code is finally working!",
    "Paula sounds better than ever with this mod",
    "Just finished a new chiptune track",
    "Anyone tried the new NeoBench kernel?",
    "The 68030 makes such a difference",
    "Fast RAM vs Chip RAM debate again? lol",
    "I need help with my sprites, they keep flickering"
};

void simulate_activity() {
    sim_rng = sim_rng * 1103515245 + 12345;
    if ((sim_rng >> 16) % 30 != 0) return; // ~3% chance per check

    if (channel_count == 0) return;
    int ci = (sim_rng >> 8) % channel_count;
    if (!channels[ci].active) return;

    int ni = (sim_rng >> 12) % 8;
    int mi = (sim_rng >> 4) % 15;

    // Don't generate messages from self
    if (neo_strcmp(sim_nicks[ni], server.nick) == 0) return;

    add_message(&channels[ci], sim_nicks[ni], sim_msgs[mi], 0);

    // Highlight check
    if (neo_strncmp(sim_msgs[mi], server.nick, neo_strlen(server.nick)) == 0) {
        // Nick highlight - could flash or beep
    }
}

// --- UI ---
void draw_ui() {
    int w = neo::display::get_width();
    int h = neo::display::get_height();
    int sidebar_width = 16;
    int msg_area_width = w - sidebar_width - 1;

    neo::display::clear();

    // Title bar
    neo::display::set_color(15, 1);
    neo::display::puts(" NeoIRC v1.0 ");
    if (server.connected) {
        neo::display::set_color(10, 1);
        neo::display::printf("| %s@%s ", server.nick, server.host);
    } else {
        neo::display::set_color(12, 1);
        neo::display::puts("| Not connected ");
    }
    int used_title = 14 + (server.connected ? neo_strlen(server.nick) + neo_strlen(server.host) + 3 : 15);
    for (int i = used_title; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);

    // Channel tabs
    neo::display::set_cursor(0, 1);
    for (int i = 0; i < channel_count; i++) {
        if (!channels[i].active) continue;
        if (i == active_channel) {
            neo::display::set_color(0, 7);
        } else {
            neo::display::set_color(8, 0);
        }
        neo::display::printf(" %s ", channels[i].name);
    }
    neo::display::set_color(7, 0);
    neo::display::clear_eol();

    // Topic
    if (active_channel < channel_count && channels[active_channel].active) {
        Channel& ch = channels[active_channel];
        neo::display::set_cursor(0, 2);
        neo::display::set_color(0, 6);
        neo::display::puts(" Topic: ");
        if (ch.topic[0]) {
            int tlen = neo_strlen(ch.topic);
            int maxdisp = w - 8;
            for (int i = 0; i < tlen && i < maxdisp; i++) neo::display::putchar(ch.topic[i]);
        } else {
            neo::display::puts("(no topic set)");
        }
        neo::display::clear_eol();
        neo::display::set_color(7, 0);
    }

    // Separator
    neo::display::set_cursor(0, 3);
    for (int i = 0; i < w; i++) {
        if (i == msg_area_width) neo::display::putchar('+');
        else neo::display::putchar('-');
    }

    // Messages
    if (active_channel < channel_count && channels[active_channel].active) {
        Channel& ch = channels[active_channel];
        int msg_lines = h - 6; // space for header, topic, separator, input, status
        int start = ch.msg_count - msg_lines;
        if (start < 0) start = 0;

        for (int i = start; i < ch.msg_count && (i - start) < msg_lines; i++) {
            ChannelMessage& m = ch.messages[i];
            neo::display::set_cursor(0, 4 + (i - start));

            // Timestamp
            int hrs = (m.timestamp / 3600) % 24;
            int mins = (m.timestamp / 60) % 60;
            neo::display::set_fg(8); // Dark gray
            neo::display::printf("[%02d:%02d] ", hrs, mins);

            switch (m.type) {
                case 0: // Normal message
                    neo::display::set_fg(nick_color(m.nick));
                    neo::display::printf("<%s> ", m.nick);
                    neo::display::set_fg(7);
                    // Check for nick highlight
                    if (neo_strncmp(m.text, server.nick, neo_strlen(server.nick)) == 0) {
                        neo::display::set_fg(14); // Yellow highlight
                    }
                    neo::display::puts(m.text);
                    neo::display::set_fg(7);
                    break;
                case 1: // Action /me
                    neo::display::set_fg(13);
                    neo::display::printf("* %s %s", m.nick, m.text);
                    neo::display::set_fg(7);
                    break;
                case 2: // Join
                    neo::display::set_fg(10);
                    neo::display::printf("--> %s has joined", m.nick);
                    neo::display::set_fg(7);
                    break;
                case 3: // Part
                    neo::display::set_fg(12);
                    neo::display::printf("<-- %s has left (%s)", m.nick, m.text);
                    neo::display::set_fg(7);
                    break;
                case 4: // System
                    neo::display::set_fg(11);
                    neo::display::printf("*** %s", m.text);
                    neo::display::set_fg(7);
                    break;
                case 5: // Notice
                    neo::display::set_fg(14);
                    neo::display::printf("-%s- %s", m.nick, m.text);
                    neo::display::set_fg(7);
                    break;
            }
        }

        // User list sidebar
        neo::display::set_fg(8);
        for (int y = 4; y < h - 2; y++) {
            neo::display::set_cursor(msg_area_width, y);
            neo::display::putchar('|');
        }

        neo::display::set_cursor(msg_area_width + 1, 4);
        neo::display::set_color(14, 0);
        neo::display::printf("Users(%d)", ch.user_count);
        neo::display::set_color(7, 0);

        for (int i = 0; i < ch.user_count && i < (h - 7); i++) {
            neo::display::set_cursor(msg_area_width + 1, 5 + i);
            neo::display::set_fg(nick_color(ch.users[i]));
            // Truncate nick
            char dispnick[16];
            neo_strncpy(dispnick, ch.users[i], sidebar_width - 1);
            dispnick[sidebar_width - 1] = 0;
            neo::display::puts(dispnick);
        }
        neo::display::set_fg(7);
    }

    // Input separator
    neo::display::set_cursor(0, h - 2);
    for (int i = 0; i < w; i++) neo::display::putchar('-');

    // Input line
    neo::display::set_cursor(0, h - 1);
    neo::display::set_fg(7);
}

// --- IRC Protocol simulation ---
void irc_connect(const char* host, int port, const char* nick, const char* user) {
    neo::display::clear();
    neo::display::puts("Connecting to IRC server...\n");
    neo::timer::delay_ms(300);

    neo_strncpy(server.host, host, 127);
    server.port = port;
    neo_strncpy(server.nick, nick, 31);
    neo_strncpy(server.username, user, 31);
    neo_strcpy(server.realname, "NeoIRC User");

    neo::display::printf("NICK %s\n", nick);
    neo::timer::delay_ms(100);
    neo::display::printf("USER %s 0 * :%s\n", user, server.realname);
    neo::timer::delay_ms(200);

    neo::display::puts(":server 001 :Welcome to the NeoIRC Network\n");
    neo::display::puts(":server 002 :Your host is neobench.irc, running NeoIRCd v1.0\n");
    neo::display::puts(":server 003 :This server was created today\n");
    neo::display::puts(":server 375 :- neobench.irc Message of the Day -\n");
    neo::display::puts(":server 372 :- Welcome to the NeoBench IRC Network!\n");
    neo::display::puts(":server 372 :- Enjoy your stay and be nice.\n");
    neo::display::puts(":server 376 :End of /MOTD command.\n");
    neo::timer::delay_ms(500);

    server.connected = true;

    // Create server messages channel
    Channel* status = create_channel("*status");
    if (status) {
        add_message(status, "server", "Connected to neobench.irc", 4);
        add_message(status, "server", "Welcome to the NeoIRC Network!", 4);
        neo_strcpy(status->topic, "Server Messages");
    }

    neo::display::puts("\nConnected! Use /join #channel to join a channel.\n");
    neo::timer::delay_ms(500);
}

void irc_join(const char* channel_name) {
    Channel* ch = find_channel(channel_name);
    if (!ch) ch = create_channel(channel_name);
    if (!ch) return;

    neo::display::printf("JOIN %s\n", channel_name);
    neo::timer::delay_ms(200);

    // Add self
    add_user(ch, server.nick);

    // Simulate other users
    int num_users = 3 + (neo::timer::get_ticks() % 5);
    for (int i = 0; i < num_users && i < 8; i++) {
        add_user(ch, sim_nicks[i]);
    }

    // Set topic
    ksprintf(ch->topic, sizeof(ch->topic), "Welcome to %s - Amiga enthusiasts unite!", channel_name);

    // Join messages
    char joinmsg[128];
    ksprintf(joinmsg, sizeof(joinmsg), "%s has joined %s", server.nick, channel_name);
    add_message(ch, server.nick, "", 2);
    add_message(ch, "server", ch->topic, 4);

    // Switch to new channel
    active_channel = find_channel_idx(channel_name);
}

void irc_part(const char* channel_name, const char* reason) {
    int idx = find_channel_idx(channel_name);
    if (idx < 0) return;

    add_message(&channels[idx], server.nick, reason[0] ? reason : "Leaving", 3);
    channels[idx].active = false;

    // Switch to another channel
    if (active_channel == idx) {
        active_channel = 0;
        for (int i = 0; i < channel_count; i++) {
            if (channels[i].active) { active_channel = i; break; }
        }
    }
}

void irc_privmsg(const char* target, const char* msg) {
    Channel* ch = find_channel(target);
    if (!ch) {
        // PM - create channel for DM
        ch = create_channel(target);
        if (!ch) return;
        add_user(ch, server.nick);
        add_user(ch, target);
    }
    add_message(ch, server.nick, msg, 0);
}

// --- Command parser ---
void process_command(const char* input) {
    if (input[0] != '/') {
        // Regular message to current channel
        if (active_channel < channel_count && channels[active_channel].active) {
            const char* target = channels[active_channel].name;
            if (target[0] != '*') { // Don't send to status
                irc_privmsg(target, input);
            }
        }
        return;
    }

    // Commands
    const char* cmd = input + 1;

    if (neo_strncmp(cmd, "join ", 5) == 0) {
        const char* chan = cmd + 5;
        while (*chan == ' ') chan++;
        if (chan[0]) irc_join(chan);
    }
    else if (neo_strncmp(cmd, "part", 4) == 0) {
        const char* rest = cmd + 4;
        while (*rest == ' ') rest++;
        const char* target;
        if (rest[0] == '#') {
            target = rest;
        } else if (active_channel < channel_count) {
            target = channels[active_channel].name;
        } else return;
        irc_part(target, rest);
    }
    else if (neo_strncmp(cmd, "msg ", 4) == 0) {
        const char* rest = cmd + 4;
        while (*rest == ' ') rest++;
        char target[64];
        int ti = 0;
        while (*rest && *rest != ' ' && ti < 63) target[ti++] = *rest++;
        target[ti] = 0;
        while (*rest == ' ') rest++;
        if (target[0] && rest[0]) irc_privmsg(target, rest);
    }
    else if (neo_strncmp(cmd, "nick ", 5) == 0) {
        const char* newnick = cmd + 5;
        while (*newnick == ' ') newnick++;
        if (newnick[0]) {
            // Update nick in all channels
            for (int i = 0; i < channel_count; i++) {
                if (!channels[i].active) continue;
                remove_user(&channels[i], server.nick);
                add_user(&channels[i], newnick);
                char nickmsg[128];
                ksprintf(nickmsg, sizeof(nickmsg), "%s is now known as %s", server.nick, newnick);
                add_message(&channels[i], "server", nickmsg, 4);
            }
            neo_strncpy(server.nick, newnick, 31);
        }
    }
    else if (neo_strncmp(cmd, "me ", 3) == 0) {
        const char* action = cmd + 3;
        if (active_channel < channel_count && channels[active_channel].active) {
            add_message(&channels[active_channel], server.nick, action, 1);
        }
    }
    else if (neo_strncmp(cmd, "topic ", 6) == 0) {
        const char* newtopic = cmd + 6;
        if (active_channel < channel_count && channels[active_channel].active) {
            neo_strncpy(channels[active_channel].topic, newtopic, 255);
            char topmsg[300];
            ksprintf(topmsg, sizeof(topmsg), "%s changed the topic to: %s", server.nick, newtopic);
            add_message(&channels[active_channel], "server", topmsg, 4);
        }
    }
    else if (neo_strncmp(cmd, "names", 5) == 0) {
        if (active_channel < channel_count && channels[active_channel].active) {
            Channel& ch = channels[active_channel];
            char namelist[512] = "Users: ";
            for (int i = 0; i < ch.user_count; i++) {
                if (i > 0) neo_strcat(namelist, ", ");
                neo_strcat(namelist, ch.users[i]);
            }
            add_message(&ch, "server", namelist, 4);
        }
    }
    else if (neo_strncmp(cmd, "quit", 4) == 0) {
        // Handled in main loop
    }
    else if (neo_strncmp(cmd, "help", 4) == 0) {
        Channel* ch = (active_channel < channel_count) ? &channels[active_channel] : nullptr;
        if (ch) {
            add_message(ch, "help", "Commands: /join /part /msg /nick /me /topic /names /quit /help", 4);
            add_message(ch, "help", "Tab: switch channels  PgUp/PgDn: scroll", 4);
        }
    }
}

} // namespace neoirc

extern "C" void app_main(int argc, char** argv) {
    using namespace neoirc;

    neo_memset(&server, 0, sizeof(server));
    neo_memset(channels, 0, sizeof(channels));
    channel_count = 0;
    active_channel = 0;
    sim_rng = neo::timer::get_ticks();

    // Connection setup
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+================================+\n");
    neo::display::puts("|   NeoIRC v1.0 - IRC Client    |\n");
    neo::display::puts("+================================+\n");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');

    char host[128], nick[32], portstr[8];
    neo::console::getline(host, sizeof(host), "Server [irc.neobench.local]: ");
    if (host[0] == 0) neo_strcpy(host, "irc.neobench.local");
    neo::console::getline(portstr, sizeof(portstr), "Port [6667]: ");
    int port = 6667;
    if (portstr[0]) {
        port = 0;
        for (int i = 0; portstr[i] >= '0' && portstr[i] <= '9'; i++)
            port = port * 10 + (portstr[i] - '0');
    }
    neo::console::getline(nick, sizeof(nick), "Nickname [AmigaUser]: ");
    if (nick[0] == 0) neo_strcpy(nick, "AmigaUser");

    irc_connect(host, port, nick, nick);

    // Auto-join default channel
    irc_join("#amiga");

    draw_ui();

    char input[INODE_SIZE];
    bool running = true;
    unsigned int last_sim = neo::timer::get_ticks();

    while (running) {
        // Simulate IRC activity periodically
        unsigned int now = neo::timer::get_ticks();
        if (now - last_sim > 100) { // ~2 seconds
            simulate_activity();
            last_sim = now;
            draw_ui();

            // Restore cursor to input line
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 1);
        }

        if (!neo::keyboard::key_available()) {
            neo::proc::yield();
            continue;
        }

        unsigned char sc = neo::keyboard::read_scancode();
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        if (sc == 0x42) { // Tab - switch channel
            if (channel_count > 0) {
                active_channel = (active_channel + 1) % channel_count;
                while (active_channel < channel_count && !channels[active_channel].active) {
                    active_channel = (active_channel + 1) % channel_count;
                }
                draw_ui();
            }
            continue;
        }

        if (sc == 0x4E) { // PgDown - not a standard scancode, using right
            // Could implement scrolling
            continue;
        }

        if (ch == '\n' || sc == 0x44) {
            // Get input
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 1);
            neo::display::clear_eol();
            neo::console::getline(input, sizeof(input), "> ");

            if (input[0] == 0) {
                draw_ui();
                continue;
            }

            neo::console::history_add(input);

            // Check for quit
            if (neo_strncmp(input, "/quit", 5) == 0) {
                running = false;
                continue;
            }

            process_command(input);
            draw_ui();
        }
    }

    // Disconnect
    neo::display::clear();
    neo::display::set_color(7, 0);
    if (server.connected) {
        neo::display::puts("QUIT :Leaving\n");
        neo::display::puts("Connection closed.\n");
    }
    neo::display::puts("NeoIRC session ended.\n");
}
