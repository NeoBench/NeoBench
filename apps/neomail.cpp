#include "../include/neobench.h"
#include "../lib/string.h"

// NeoMail - Email Client for NeoBench
// POP3/SMTP client, inbox, compose, reply, address book, folders

namespace neomail {

// --- Account Settings ---
struct Account {
    char name[64];
    char email[128];
    char pop_server[128];
    int pop_port;
    char smtp_server[128];
    int smtp_port;
    char username[64];
    char password[64];
    bool configured;
};

static Account account;

// --- Message ---
struct Message {
    int id;
    char from[128];
    char to[128];
    char subject[INODE_SIZE];
    char date[32];
    char body[1024];
    bool read;
    bool deleted;
    bool valid;
    int folder; // 0=inbox, 1=sent, 2=drafts, 3=trash
};

static const int MAX_MESSAGES = 64;
static Message messages[MAX_MESSAGES];
static int msg_count = 0;

// --- Folders ---
static const char* folder_names[] = {"Inbox", "Sent", "Drafts", "Trash"};
static const int NUM_FOLDERS = 4;
static int current_folder = 0;

// --- Address Book ---
struct Contact {
    char name[64];
    char email[128];
    bool valid;
};

static const int MAX_CONTACTS = 32;
static Contact contacts[MAX_CONTACTS];
static int contact_count = 0;

// --- Simulated POP3 Session ---
enum Pop3State {
    POP3_DISCONNECTED, POP3_CONNECTED, POP3_AUTHORIZED
};

void pop3_simulate_check() {
    neo::display::puts("Connecting to POP3 server...\n");
    neo::timer::delay_ms(300);
    neo::display::printf("  CONN %s:%d\n", account.pop_server, account.pop_port);
    neo::display::puts("  +OK POP3 server ready\n");
    neo::timer::delay_ms(200);

    neo::display::printf("  USER %s\n", account.username);
    neo::display::puts("  +OK\n");
    neo::timer::delay_ms(100);

    neo::display::puts("  PASS ****\n");
    neo::display::puts("  +OK Mailbox open\n");
    neo::timer::delay_ms(200);

    neo::display::puts("  STAT\n");

    // Generate some sample messages
    unsigned int rng = neo::timer::get_ticks();
    int new_msgs = 2 + (rng % 3);
    neo::display::printf("  +OK %d messages\n", new_msgs);

    const char* sample_from[] = {
        "alice@example.com", "bob@amiga.org", "admin@neobench.local",
        "news@commodore.net", "support@aminet.net"
    };
    const char* sample_subjects[] = {
        "Welcome to NeoMail!", "Re: Amiga development",
        "System notification", "New Aminet uploads",
        "Your account details"
    };
    const char* sample_bodies[] = {
        "Hello and welcome to NeoMail, the email client for NeoBench.\n\n"
        "You can read, compose, and manage emails right from your Amiga.\n\n"
        "Best regards,\nThe NeoMail Team",

        "Hi there,\n\nI've been working on some new demos for the A500.\n"
        "The copper effects are looking great. Want to collaborate?\n\n"
        "Cheers,\nBob",

        "SYSTEM NOTIFICATION\n\nYour NeoBench system has been running for\n"
        "24 hours without issues. Memory usage is nominal.\n\n"
        "-- NeoBench Admin",

        "New uploads on Aminet this week:\n\n"
        "- DirOpus 5.82 (util/dir)\n"
        "- MagicWB 2.0p (util/wb)\n"
        "- AGA Tetris 1.2 (game/think)\n\n"
        "Visit aminet.net for more.",

        "Your NeoMail account has been configured.\n\n"
        "Server: mail.neobench.local\n"
        "Username: user\n\nEnjoy!"
    };

    for (int i = 0; i < new_msgs && msg_count < MAX_MESSAGES; i++) {
        int idx = (rng + i) % 5;
        Message& m = messages[msg_count];
        m.id = msg_count + 1;
        neo_strncpy(m.from, sample_from[idx], 127);
        m.from[127] = 0;
        neo_strncpy(m.to, account.email, 127);
        m.to[127] = 0;
        neo_strncpy(m.subject, sample_subjects[idx], 255);
        m.subject[255] = 0;
        ksprintf(m.date, 32, "2024-01-%02d 10:%02d", 15 + i, 30 + i * 5);
        neo_strncpy(m.body, sample_bodies[idx], 1023);
        m.body[1023] = 0;
        m.read = false;
        m.deleted = false;
        m.valid = true;
        m.folder = 0; // Inbox

        neo::display::printf("  RETR %d\n", i + 1);
        neo::display::printf("  +OK %d octets\n", neo_strlen(m.body));
        neo::timer::delay_ms(150);
        msg_count++;
    }

    neo::display::puts("  QUIT\n");
    neo::display::puts("  +OK Bye\n\n");
    neo::display::printf("Received %d new message(s).\n\n", new_msgs);
}

// --- SMTP Simulation ---
void smtp_send(const char* to, const char* subject, const char* body) {
    neo::display::puts("Connecting to SMTP server...\n");
    neo::timer::delay_ms(300);
    neo::display::printf("  CONN %s:%d\n", account.smtp_server, account.smtp_port);
    neo::display::puts("  220 SMTP ready\n");
    neo::timer::delay_ms(100);

    neo::display::puts("  HELO neobench.local\n");
    neo::display::puts("  250 OK\n");
    neo::timer::delay_ms(100);

    neo::display::printf("  MAIL FROM:<%s>\n", account.email);
    neo::display::puts("  250 OK\n");
    neo::timer::delay_ms(100);

    neo::display::printf("  RCPT TO:<%s>\n", to);
    neo::display::puts("  250 OK\n");
    neo::timer::delay_ms(100);

    neo::display::puts("  DATA\n");
    neo::display::puts("  354 Start mail input\n");
    neo::timer::delay_ms(200);
    neo::display::puts("  .\n");
    neo::display::puts("  250 OK: Message queued\n");
    neo::timer::delay_ms(100);

    neo::display::puts("  QUIT\n");
    neo::display::puts("  221 Bye\n\n");
    neo::display::puts("Message sent successfully.\n\n");

    // Save to sent folder
    if (msg_count < MAX_MESSAGES) {
        Message& m = messages[msg_count];
        m.id = msg_count + 1;
        neo_strncpy(m.from, account.email, 127);
        neo_strncpy(m.to, to, 127);
        neo_strncpy(m.subject, subject, 255);
        neo_strncpy(m.body, body, 1023);
        ksprintf(m.date, 32, "2024-01-20 12:00");
        m.read = true;
        m.deleted = false;
        m.valid = true;
        m.folder = 1; // Sent
        msg_count++;
    }
}

// --- UI ---
void draw_header() {
    int w = neo::display::get_width();
    neo::display::set_color(15, 1);
    neo::display::puts(" NeoMail v1.0 ");
    neo::display::set_color(14, 1);
    neo::display::printf("| %s ", account.email);
    neo::display::set_color(7, 1);
    int used = 15 + neo_strlen(account.email) + 3;
    for (int i = used; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');
}

void draw_folder_bar() {
    int w = neo::display::get_width();
    for (int f = 0; f < NUM_FOLDERS; f++) {
        if (f == current_folder) {
            neo::display::set_color(0, 7); // Selected
        } else {
            neo::display::set_color(7, 0);
        }
        neo::display::printf(" %s ", folder_names[f]);

        // Count messages in folder
        int cnt = 0, unread = 0;
        for (int i = 0; i < msg_count; i++) {
            if (messages[i].valid && !messages[i].deleted && messages[i].folder == f) {
                cnt++;
                if (!messages[i].read) unread++;
            }
        }
        if (unread > 0) {
            neo::display::printf("(%d/%d)", unread, cnt);
        } else {
            neo::display::printf("(%d)", cnt);
        }
        neo::display::putchar(' ');
    }
    neo::display::set_color(7, 0);
    int used = 0; // approximate
    for (int i = used; i < w; i++) {}
    neo::display::putchar('\n');
    // Separator
    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::putchar('\n');
}

void show_message_list() {
    neo::display::clear();
    draw_header();
    draw_folder_bar();

    neo::display::set_color(14, 0);
    neo::display::puts("  #  | Status | From                    | Subject                          | Date\n");
    neo::display::set_color(7, 0);
    int w = neo::display::get_width();
    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::putchar('\n');

    int displayed = 0;
    for (int i = 0; i < msg_count; i++) {
        if (!messages[i].valid || messages[i].deleted) continue;
        if (messages[i].folder != current_folder) continue;

        if (!messages[i].read) {
            neo::display::set_fg(15); // White = unread
            neo::display::printf(" %2d  |  NEW   | %-23s | %-32s | %s\n",
                                 messages[i].id,
                                 messages[i].from,
                                 messages[i].subject,
                                 messages[i].date);
        } else {
            neo::display::set_fg(8); // Dark gray = read
            neo::display::printf(" %2d  |  read  | %-23s | %-32s | %s\n",
                                 messages[i].id,
                                 messages[i].from,
                                 messages[i].subject,
                                 messages[i].date);
        }
        displayed++;
    }
    neo::display::set_fg(7);

    if (displayed == 0) {
        neo::display::puts("\n  (No messages in this folder)\n");
    }

    neo::display::putchar('\n');
    neo::display::set_color(0, 3);
    neo::display::puts(" [r]ead [c]ompose [d]elete [f]older [a]ddrbook [k]check [s]ettings [q]uit ");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');
}

void show_message(int msg_id) {
    Message* m = nullptr;
    for (int i = 0; i < msg_count; i++) {
        if (messages[i].valid && messages[i].id == msg_id) { m = &messages[i]; break; }
    }
    if (!m) { neo::display::puts("Message not found.\n"); return; }

    m->read = true;

    neo::display::clear();
    neo::display::set_color(14, 1);
    int w = neo::display::get_width();
    neo::display::puts(" Message ");
    for (int i = 9; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');

    neo::display::set_fg(11);
    neo::display::printf("  From:    %s\n", m->from);
    neo::display::printf("  To:      %s\n", m->to);
    neo::display::printf("  Subject: %s\n", m->subject);
    neo::display::printf("  Date:    %s\n", m->date);
    neo::display::set_fg(7);

    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::putchar('\n');
    neo::display::putchar('\n');
    neo::display::puts(m->body);
    neo::display::putchar('\n');
    neo::display::putchar('\n');

    for (int i = 0; i < w; i++) neo::display::putchar('-');
    neo::display::putchar('\n');
    neo::display::set_color(0, 3);
    neo::display::puts(" [r]eply [d]elete [b]ack ");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');

    // Wait for action
    while (true) {
        if (!neo::keyboard::key_available()) { neo::proc::yield(); continue; }
        unsigned char sc = neo::keyboard::read_scancode();
        char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
        if (ch == 'b' || ch == 'B') break;
        if (ch == 'd' || ch == 'D') {
            m->folder = 3; // Move to trash
            m->deleted = (current_folder == 3); // Permanent if already in trash
            neo::display::puts("Message moved to trash.\n");
            neo::timer::delay_ms(500);
            break;
        }
        if (ch == 'r' || ch == 'R') {
            // Reply
            char reply_body[1024];
            char reply_subj[INODE_SIZE];
            if (neo_strncmp(m->subject, "Re: ", 4) != 0) {
                ksprintf(reply_subj, sizeof(reply_subj), "Re: %s", m->subject);
            } else {
                neo_strncpy(reply_subj, m->subject, 255);
            }

            neo::display::clear();
            neo::display::set_color(14, 1);
            neo::display::puts(" Compose Reply ");
            for (int i = 15; i < w; i++) neo::display::putchar(' ');
            neo::display::set_color(7, 0);
            neo::display::putchar('\n');

            neo::display::printf("  To:      %s\n", m->from);
            neo::display::printf("  Subject: %s\n\n", reply_subj);
            neo::display::puts("Enter reply (end with . on a line by itself):\n");

            int body_pos = 0;
            char linebuf[INODE_SIZE];
            while (body_pos < 900) {
                neo::console::getline(linebuf, sizeof(linebuf), "> ");
                if (neo_strcmp(linebuf, ".") == 0) break;
                int ll = neo_strlen(linebuf);
                if (body_pos + ll + 1 < 1023) {
                    neo_memcpy(reply_body + body_pos, linebuf, ll);
                    body_pos += ll;
                    reply_body[body_pos++] = '\n';
                }
            }
            // Append quoted original
            neo_strcat(reply_body, "\n--- Original Message ---\n");
            int orig_space = 1023 - neo_strlen(reply_body);
            if (orig_space > 0) {
                int copy_len = neo_strlen(m->body);
                if (copy_len > orig_space) copy_len = orig_space;
                neo_memcpy(reply_body + neo_strlen(reply_body), m->body, copy_len);
                reply_body[neo_strlen(reply_body)] = 0;
            }

            neo::display::puts("\nSend? [y/n]: ");
            neo::console::getline(linebuf, sizeof(linebuf), "");
            if (linebuf[0] == 'y' || linebuf[0] == 'Y') {
                smtp_send(m->from, reply_subj, reply_body);
            }
            break;
        }
    }
}

void compose_message() {
    int w = neo::display::get_width();
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts(" Compose New Message ");
    for (int i = 20; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');
    neo::display::putchar('\n');

    char to[128], subject[INODE_SIZE], body[1024];
    neo::console::getline(to, sizeof(to), "To: ");
    if (to[0] == 0) return;
    neo::console::getline(subject, sizeof(subject), "Subject: ");
    neo::display::puts("\nMessage body (end with . on a line by itself):\n");

    int body_pos = 0;
    char linebuf[INODE_SIZE];
    while (body_pos < 900) {
        neo::console::getline(linebuf, sizeof(linebuf), "");
        if (neo_strcmp(linebuf, ".") == 0) break;
        int ll = neo_strlen(linebuf);
        if (body_pos + ll + 1 < 1023) {
            neo_memcpy(body + body_pos, linebuf, ll);
            body_pos += ll;
            body[body_pos++] = '\n';
        }
    }
    body[body_pos] = 0;

    neo::display::puts("\nSend? [y/n]: ");
    neo::console::getline(linebuf, sizeof(linebuf), "");
    if (linebuf[0] == 'y' || linebuf[0] == 'Y') {
        smtp_send(to, subject, body);
    } else {
        // Save to drafts
        if (msg_count < MAX_MESSAGES) {
            Message& m = messages[msg_count];
            m.id = msg_count + 1;
            neo_strncpy(m.from, account.email, 127);
            neo_strncpy(m.to, to, 127);
            neo_strncpy(m.subject, subject, 255);
            neo_strncpy(m.body, body, 1023);
            ksprintf(m.date, 32, "2024-01-20 12:00");
            m.read = true;
            m.deleted = false;
            m.valid = true;
            m.folder = 2; // Drafts
            msg_count++;
        }
        neo::display::puts("Message saved to Drafts.\n");
        neo::timer::delay_ms(500);
    }
}

void show_address_book() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+============================+\n");
    neo::display::puts("|       Address Book         |\n");
    neo::display::puts("+============================+\n");
    neo::display::set_color(7, 0);

    if (contact_count == 0) {
        neo::display::puts("\n  (No contacts)\n");
    }
    for (int i = 0; i < contact_count; i++) {
        if (!contacts[i].valid) continue;
        neo::display::printf("  %d. %-30s <%s>\n", i + 1,
                             contacts[i].name, contacts[i].email);
    }

    neo::display::puts("\n[a]dd contact  [d]elete  [b]ack\n");

    char input[INODE_SIZE];
    while (true) {
        if (!neo::keyboard::key_available()) { neo::proc::yield(); continue; }
        unsigned char sc = neo::keyboard::read_scancode();
        char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
        if (ch == 'b' || ch == 'B') break;
        if (ch == 'a' || ch == 'A') {
            if (contact_count < MAX_CONTACTS) {
                neo::display::putchar('\n');
                neo::console::getline(contacts[contact_count].name, 64, "Name: ");
                neo::console::getline(contacts[contact_count].email, 128, "Email: ");
                if (contacts[contact_count].name[0] && contacts[contact_count].email[0]) {
                    contacts[contact_count].valid = true;
                    contact_count++;
                    neo::display::puts("Contact added.\n");
                }
            }
            break;
        }
        if (ch == 'd' || ch == 'D') {
            neo::console::getline(input, sizeof(input), "Delete #: ");
            int sel = 0;
            for (int i = 0; input[i] >= '0' && input[i] <= '9'; i++)
                sel = sel * 10 + (input[i] - '0');
            if (sel >= 1 && sel <= contact_count) {
                contacts[sel - 1].valid = false;
                neo::display::puts("Contact deleted.\n");
            }
            break;
        }
    }
}

void show_settings() {
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+============================+\n");
    neo::display::puts("|     Account Settings       |\n");
    neo::display::puts("+============================+\n");
    neo::display::set_color(7, 0);

    neo::display::printf("\n  Name:        %s\n", account.name);
    neo::display::printf("  Email:       %s\n", account.email);
    neo::display::printf("  POP3 Server: %s:%d\n", account.pop_server, account.pop_port);
    neo::display::printf("  SMTP Server: %s:%d\n", account.smtp_server, account.smtp_port);
    neo::display::printf("  Username:    %s\n", account.username);
    neo::display::puts("  Password:    ****\n");

    neo::display::puts("\n[e]dit settings  [b]ack\n");

    while (true) {
        if (!neo::keyboard::key_available()) { neo::proc::yield(); continue; }
        unsigned char sc = neo::keyboard::read_scancode();
        char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());
        if (ch == 'b' || ch == 'B') break;
        if (ch == 'e' || ch == 'E') {
            neo::display::puts("\nEnter new values (blank to keep current):\n");
            char tmp[128];
            neo::console::getline(tmp, sizeof(tmp), "Name: ");
            if (tmp[0]) neo_strncpy(account.name, tmp, 63);
            neo::console::getline(tmp, sizeof(tmp), "Email: ");
            if (tmp[0]) neo_strncpy(account.email, tmp, 127);
            neo::console::getline(tmp, sizeof(tmp), "POP3 Server: ");
            if (tmp[0]) neo_strncpy(account.pop_server, tmp, 127);
            neo::console::getline(tmp, sizeof(tmp), "SMTP Server: ");
            if (tmp[0]) neo_strncpy(account.smtp_server, tmp, 127);
            neo::console::getline(tmp, sizeof(tmp), "Username: ");
            if (tmp[0]) neo_strncpy(account.username, tmp, 63);
            neo::console::getline(tmp, sizeof(tmp), "Password: ");
            if (tmp[0]) neo_strncpy(account.password, tmp, 63);
            neo::display::puts("Settings updated.\n");
            neo::timer::delay_ms(500);
            break;
        }
    }
}

} // namespace neomail

extern "C" void app_main(int argc, char** argv) {
    using namespace neomail;

    // Initialize
    neo_memset(&account, 0, sizeof(account));
    neo_memset(messages, 0, sizeof(messages));
    neo_memset(contacts, 0, sizeof(contacts));
    msg_count = 0;
    contact_count = 0;
    current_folder = 0;

    // Default account
    neo_strcpy(account.name, "Amiga User");
    neo_strcpy(account.email, "user@neobench.local");
    neo_strcpy(account.pop_server, "mail.neobench.local");
    account.pop_port = 110;
    neo_strcpy(account.smtp_server, "mail.neobench.local");
    account.smtp_port = 25;
    neo_strcpy(account.username, "user");
    neo_strcpy(account.password, "amiga");
    account.configured = true;

    // Default contacts
    neo_strcpy(contacts[0].name, "Alice Smith");
    neo_strcpy(contacts[0].email, "alice@example.com");
    contacts[0].valid = true;
    neo_strcpy(contacts[1].name, "Bob Jones");
    neo_strcpy(contacts[1].email, "bob@amiga.org");
    contacts[1].valid = true;
    contact_count = 2;

    // Welcome
    neo::display::clear();
    neo::display::set_color(14, 1);
    neo::display::puts("+====================================+\n");
    neo::display::puts("|  NeoMail v1.0 - Email for Amiga   |\n");
    neo::display::puts("+====================================+\n");
    neo::display::set_color(7, 0);
    neo::display::putchar('\n');

    // Auto-check mail
    pop3_simulate_check();

    char input[INODE_SIZE];
    bool running = true;

    while (running) {
        show_message_list();

        // Wait for command
        while (true) {
            if (!neo::keyboard::key_available()) { neo::proc::yield(); continue; }
            unsigned char sc = neo::keyboard::read_scancode();
            char ch = neo::keyboard::translate(sc, neo::keyboard::is_shift_down());

            if (ch == 'q' || ch == 'Q') { running = false; break; }
            if (ch == 'r' || ch == 'R') {
                // Read message
                int h = neo::display::get_height();
                neo::display::set_cursor(0, h - 1);
                neo::console::getline(input, sizeof(input), "Read message #: ");
                int num = 0;
                for (int i = 0; input[i] >= '0' && input[i] <= '9'; i++)
                    num = num * 10 + (input[i] - '0');
                if (num > 0) show_message(num);
                break;
            }
            if (ch == 'c' || ch == 'C') { compose_message(); break; }
            if (ch == 'd' || ch == 'D') {
                int h = neo::display::get_height();
                neo::display::set_cursor(0, h - 1);
                neo::console::getline(input, sizeof(input), "Delete message #: ");
                int num = 0;
                for (int i = 0; input[i] >= '0' && input[i] <= '9'; i++)
                    num = num * 10 + (input[i] - '0');
                for (int i = 0; i < msg_count; i++) {
                    if (messages[i].valid && messages[i].id == num) {
                        messages[i].folder = 3; // Move to trash
                        break;
                    }
                }
                break;
            }
            if (ch == 'f' || ch == 'F') {
                current_folder = (current_folder + 1) % NUM_FOLDERS;
                break;
            }
            if (ch == 'a' || ch == 'A') { show_address_book(); break; }
            if (ch == 'k' || ch == 'K') {
                neo::display::clear();
                pop3_simulate_check();
                neo::display::puts("Press any key...");
                while (!neo::keyboard::key_available()) neo::proc::yield();
                neo::keyboard::read_scancode();
                break;
            }
            if (ch == 's' || ch == 'S') { show_settings(); break; }
        }
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    neo::display::puts("NeoMail session ended.\n");
}
