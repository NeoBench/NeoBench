#include "../include/neobench.h"
#include "../lib/string.h"

// NeoBrowse - Text-mode Web Browser for NeoBench
// HTML 3.2 parser, HTTP/1.0, hyperlinks, history, bookmarks

namespace neobrowse {

// --- URL ---
struct Url {
    char scheme[8];   // http
    char host[128];
    int port;
    char path[INODE_SIZE];
};

void url_init(Url& u) {
    neo_memset(&u, 0, sizeof(u));
    neo_strcpy(u.scheme, "http");
    u.port = 80;
    neo_strcpy(u.path, "/");
}

bool url_parse(const char* str, Url& u) {
    url_init(u);
    const char* p = str;

    // Skip scheme
    if (neo_strncmp(p, "http://", 7) == 0) {
        neo_strcpy(u.scheme, "http");
        p += 7;
    } else if (neo_strncmp(p, "https://", 8) == 0) {
        neo_strcpy(u.scheme, "https");
        p += 8;
    }

    // Host
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < 127) u.host[hi++] = *p++;
    u.host[hi] = 0;

    // Port
    if (*p == ':') {
        p++;
        u.port = 0;
        while (*p >= '0' && *p <= '9') { u.port = u.port * 10 + (*p - '0'); p++; }
    }

    // Path
    if (*p == '/') {
        int pi = 0;
        while (*p && pi < 255) u.path[pi++] = *p++;
        u.path[pi] = 0;
    } else {
        neo_strcpy(u.path, "/");
    }
    return u.host[0] != 0;
}

void url_to_str(char* buf, int maxlen, const Url& u) {
    ksprintf(buf, maxlen, "%s://%s", u.scheme, u.host);
    if (u.port != 80) {
        char portstr[8];
        ksprintf(portstr, 8, ":%d", u.port);
        neo_strcat(buf, portstr);
    }
    neo_strcat(buf, u.path);
}

// --- HTML Token Types ---
enum TokenType {
    TOK_TEXT, TOK_TAG_OPEN, TOK_TAG_CLOSE, TOK_EOF
};

struct HtmlToken {
    TokenType type;
    char tag[32];
    char text[512];
    // For <a href="...">
    char attr_href[INODE_SIZE];
};

// --- Link storage ---
struct Link {
    char url[INODE_SIZE];
    char text[64];
};

static const int MAX_LINKS = 64;
static Link page_links[MAX_LINKS];
static int link_count = 0;

// --- Rendering State ---
struct RenderState {
    bool bold;
    bool italic;
    bool preformatted;
    int heading_level;
    bool in_list;
    bool ordered_list;
    int list_item;
    int col;
    int line;
    int screen_width;
    int fg_color;
    bool in_anchor;
    int current_link;
};

static RenderState rstate;

void render_init() {
    rstate.bold = false;
    rstate.italic = false;
    rstate.preformatted = false;
    rstate.heading_level = 0;
    rstate.in_list = false;
    rstate.ordered_list = false;
    rstate.list_item = 0;
    rstate.col = 0;
    rstate.line = 0;
    rstate.screen_width = neo::display::get_width();
    rstate.fg_color = 7;
    rstate.in_anchor = false;
    rstate.current_link = -1;
}

void render_newline() {
    neo::display::putchar('\n');
    rstate.col = 0;
    rstate.line++;
}

void render_word(const char* word, int len) {
    if (len <= 0) return;
    // Word wrap
    if (!rstate.preformatted && rstate.col + len + 1 >= rstate.screen_width) {
        render_newline();
    }
    if (rstate.col > 0 && !rstate.preformatted) {
        neo::display::putchar(' ');
        rstate.col++;
    }
    for (int i = 0; i < len; i++) {
        neo::display::putchar(word[i]);
        rstate.col++;
    }
}

void render_text(const char* text) {
    if (rstate.preformatted) {
        for (int i = 0; text[i]; i++) {
            if (text[i] == '\n') render_newline();
            else { neo::display::putchar(text[i]); rstate.col++; }
        }
        return;
    }

    // Split into words and render with wrapping
    const char* p = text;
    char word[128];
    int wi = 0;

    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            if (wi > 0) { word[wi] = 0; render_word(word, wi); wi = 0; }
            p++;
        } else {
            if (wi < 127) word[wi++] = *p;
            p++;
        }
    }
    if (wi > 0) { word[wi] = 0; render_word(word, wi); }
}

// --- HTML Entity decode ---
void decode_entities(char* text) {
    char* r = text;
    char* w = text;
    while (*r) {
        if (*r == '&') {
            if (neo_strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
            else if (neo_strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
            else if (neo_strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
            else if (neo_strncmp(r, "&quot;", 6) == 0) { *w++ = '"'; r += 6; }
            else if (neo_strncmp(r, "&nbsp;", 6) == 0) { *w++ = ' '; r += 6; }
            else { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

// --- Tag name comparison (case-insensitive) ---
char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

bool tag_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return false;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

// --- Simple HTML Parser ---
struct HtmlParser {
    const char* src;
    int pos;
    int len;
};

void parser_init(HtmlParser& p, const char* html, int length) {
    p.src = html;
    p.pos = 0;
    p.len = length;
}

bool parser_next(HtmlParser& p, HtmlToken& tok) {
    if (p.pos >= p.len) { tok.type = TOK_EOF; return false; }

    if (p.src[p.pos] == '<') {
        // Tag
        p.pos++; // skip <
        bool closing = false;
        if (p.pos < p.len && p.src[p.pos] == '/') { closing = true; p.pos++; }

        // Read tag name
        int ti = 0;
        while (p.pos < p.len && p.src[p.pos] != '>' && p.src[p.pos] != ' ' && ti < 31) {
            tok.tag[ti++] = to_lower(p.src[p.pos]);
            p.pos++;
        }
        tok.tag[ti] = 0;
        tok.attr_href[0] = 0;

        // Read attributes until >
        while (p.pos < p.len && p.src[p.pos] != '>') {
            // Look for href=
            if (neo_strncmp(p.src + p.pos, "href=\"", 6) == 0 ||
                neo_strncmp(p.src + p.pos, "href='", 6) == 0 ||
                neo_strncmp(p.src + p.pos, "HREF=\"", 6) == 0) {
                p.pos += 6;
                int hi = 0;
                char delim = p.src[p.pos - 1]; // matching quote
                // Actually we already consumed it, search for the quote
                while (p.pos < p.len && p.src[p.pos] != '"' && p.src[p.pos] != '\'' && p.src[p.pos] != '>' && hi < 255) {
                    tok.attr_href[hi++] = p.src[p.pos++];
                }
                tok.attr_href[hi] = 0;
                if (p.pos < p.len && (p.src[p.pos] == '"' || p.src[p.pos] == '\'')) p.pos++;
            } else {
                p.pos++;
            }
        }
        if (p.pos < p.len) p.pos++; // skip >

        tok.type = closing ? TOK_TAG_CLOSE : TOK_TAG_OPEN;
        tok.text[0] = 0;
        return true;
    } else {
        // Text content
        int ti = 0;
        while (p.pos < p.len && p.src[p.pos] != '<' && ti < 511) {
            tok.text[ti++] = p.src[p.pos++];
        }
        tok.text[ti] = 0;
        decode_entities(tok.text);
        tok.type = TOK_TEXT;
        tok.tag[0] = 0;
        return true;
    }
}

// --- Page rendering ---
void render_html(const char* html, int len, char* title, int title_max) {
    title[0] = 0;
    link_count = 0;
    render_init();

    HtmlParser parser;
    parser_init(parser, html, len);

    bool in_title = false;
    bool in_body = true; // render even without body tag
    HtmlToken tok;

    while (parser_next(parser, tok)) {
        if (tok.type == TOK_TAG_OPEN) {
            if (tag_eq(tok.tag, "title")) { in_title = true; }
            else if (tag_eq(tok.tag, "body")) { in_body = true; }
            else if (tag_eq(tok.tag, "h1")) {
                render_newline(); render_newline();
                neo::display::set_bold(true);
                neo::display::set_fg(14); // Yellow
                rstate.heading_level = 1;
            }
            else if (tag_eq(tok.tag, "h2")) {
                render_newline(); render_newline();
                neo::display::set_bold(true);
                neo::display::set_fg(11); // Cyan
                rstate.heading_level = 2;
            }
            else if (tag_eq(tok.tag, "h3") || tag_eq(tok.tag, "h4") ||
                     tag_eq(tok.tag, "h5") || tag_eq(tok.tag, "h6")) {
                render_newline();
                neo::display::set_bold(true);
                neo::display::set_fg(15); // White
                rstate.heading_level = 3;
            }
            else if (tag_eq(tok.tag, "p")) {
                if (rstate.col > 0) render_newline();
                render_newline();
            }
            else if (tag_eq(tok.tag, "br")) { render_newline(); }
            else if (tag_eq(tok.tag, "hr")) {
                render_newline();
                for (int i = 0; i < rstate.screen_width - 1; i++) neo::display::putchar('-');
                render_newline();
            }
            else if (tag_eq(tok.tag, "b") || tag_eq(tok.tag, "strong")) {
                neo::display::set_bold(true);
                rstate.bold = true;
            }
            else if (tag_eq(tok.tag, "i") || tag_eq(tok.tag, "em")) {
                rstate.italic = true;
                neo::display::set_fg(11);
            }
            else if (tag_eq(tok.tag, "pre")) {
                render_newline();
                neo::display::set_fg(10); // Green
                rstate.preformatted = true;
            }
            else if (tag_eq(tok.tag, "ul")) {
                render_newline();
                rstate.in_list = true;
                rstate.ordered_list = false;
                rstate.list_item = 0;
            }
            else if (tag_eq(tok.tag, "ol")) {
                render_newline();
                rstate.in_list = true;
                rstate.ordered_list = true;
                rstate.list_item = 0;
            }
            else if (tag_eq(tok.tag, "li")) {
                render_newline();
                rstate.list_item++;
                if (rstate.ordered_list) {
                    char num[8];
                    ksprintf(num, 8, "  %d. ", rstate.list_item);
                    neo::display::puts(num);
                    rstate.col += neo_strlen(num);
                } else {
                    neo::display::puts("  * ");
                    rstate.col += 4;
                }
            }
            else if (tag_eq(tok.tag, "a")) {
                rstate.in_anchor = true;
                if (link_count < MAX_LINKS && tok.attr_href[0]) {
                    rstate.current_link = link_count;
                    neo_strncpy(page_links[link_count].url, tok.attr_href, 255);
                    page_links[link_count].url[255] = 0;
                    page_links[link_count].text[0] = 0;
                    // Display link number
                    neo::display::set_fg(12); // Light red
                    char lnum[8];
                    ksprintf(lnum, 8, "[%d]", link_count + 1);
                    neo::display::puts(lnum);
                    rstate.col += neo_strlen(lnum);
                    neo::display::set_fg(9); // Blue
                }
            }
            else if (tag_eq(tok.tag, "img")) {
                neo::display::set_fg(5);
                neo::display::puts("[IMG]");
                rstate.col += 5;
                neo::display::set_fg(7);
            }
        }
        else if (tok.type == TOK_TAG_CLOSE) {
            if (tag_eq(tok.tag, "title")) { in_title = false; }
            else if (tag_eq(tok.tag, "h1") || tag_eq(tok.tag, "h2") ||
                     tag_eq(tok.tag, "h3") || tag_eq(tok.tag, "h4") ||
                     tag_eq(tok.tag, "h5") || tag_eq(tok.tag, "h6")) {
                neo::display::set_bold(false);
                neo::display::set_fg(7);
                render_newline();
                if (rstate.heading_level <= 2) {
                    for (int i = 0; i < rstate.screen_width / 2; i++) neo::display::putchar('=');
                    render_newline();
                }
                rstate.heading_level = 0;
            }
            else if (tag_eq(tok.tag, "b") || tag_eq(tok.tag, "strong")) {
                neo::display::set_bold(false); rstate.bold = false;
            }
            else if (tag_eq(tok.tag, "i") || tag_eq(tok.tag, "em")) {
                neo::display::set_fg(7); rstate.italic = false;
            }
            else if (tag_eq(tok.tag, "pre")) {
                neo::display::set_fg(7); rstate.preformatted = false;
                render_newline();
            }
            else if (tag_eq(tok.tag, "ul") || tag_eq(tok.tag, "ol")) {
                rstate.in_list = false;
                render_newline();
            }
            else if (tag_eq(tok.tag, "a")) {
                neo::display::set_fg(7);
                rstate.in_anchor = false;
                rstate.current_link = -1;
            }
            else if (tag_eq(tok.tag, "p")) {
                if (rstate.col > 0) render_newline();
            }
        }
        else if (tok.type == TOK_TEXT) {
            if (in_title) {
                neo_strncpy(title, tok.text, title_max - 1);
                title[title_max - 1] = 0;
            }
            if (in_body) {
                if (rstate.in_anchor && rstate.current_link >= 0 && rstate.current_link < MAX_LINKS) {
                    // Store link text
                    neo_strncpy(page_links[rstate.current_link].text, tok.text, 63);
                    page_links[rstate.current_link].text[63] = 0;
                    if (rstate.current_link == link_count) link_count++;
                }
                render_text(tok.text);
            }
        }
    }
    render_newline();
}

// --- HTTP Client (simulated) ---
static const char* sample_pages[] = {
    // Page 0 - default home
    "<html><head><title>NeoBrowse Home</title></head><body>"
    "<h1>Welcome to NeoBrowse</h1>"
    "<p>Your text-mode web browser for the Amiga.</p>"
    "<hr>"
    "<h2>Quick Links</h2>"
    "<ul>"
    "<li><a href=\"http://info.cern.ch/\">CERN - Where the Web began</a></li>"
    "<li><a href=\"http://amiga.org/\">Amiga Community</a></li>"
    "<li><a href=\"http://neobench.local/help\">NeoBrowse Help</a></li>"
    "<li><a href=\"http://neobench.local/about\">About NeoBrowse</a></li>"
    "</ul>"
    "<h2>Features</h2>"
    "<ol>"
    "<li>HTML 3.2 tag support</li>"
    "<li>Hyperlink navigation</li>"
    "<li>History and bookmarks</li>"
    "<li>Page source view</li>"
    "</ol>"
    "<p><b>Bold text</b> and <i>italic text</i> supported.</p>"
    "<pre>\n"
    "  _   _            ____\n"
    " | \\ | | ___  ___ | __ )\n"
    " |  \\| |/ _ \\/ _ \\|  _ \\\n"
    " | |\\  |  __/ (_) | |_) |\n"
    " |_| \\_|\\___|\\___/|____/\n"
    "</pre>"
    "</body></html>",

    // Page 1 - help
    "<html><head><title>NeoBrowse Help</title></head><body>"
    "<h1>NeoBrowse Help</h1>"
    "<p>Navigate the web using these commands:</p>"
    "<h2>Navigation</h2>"
    "<ul>"
    "<li><b>g</b> - Go to URL</li>"
    "<li><b>number + Enter</b> - Follow numbered link</li>"
    "<li><b>b</b> - Back in history</li>"
    "<li><b>f</b> - Forward in history</li>"
    "<li><b>r</b> - Reload page</li>"
    "</ul>"
    "<h2>Other Commands</h2>"
    "<ul>"
    "<li><b>s</b> - View page source</li>"
    "<li><b>k</b> - Add bookmark</li>"
    "<li><b>m</b> - Show bookmarks</li>"
    "<li><b>l</b> - Show links on page</li>"
    "<li><b>h</b> - Show history</li>"
    "<li><b>q</b> - Quit</li>"
    "</ul>"
    "<p><a href=\"http://neobench.local/\">Back to Home</a></p>"
    "</body></html>",

    // Page 2 - about
    "<html><head><title>About NeoBrowse</title></head><body>"
    "<h1>About NeoBrowse</h1>"
    "<p>NeoBrowse v1.0 - A text-mode web browser</p>"
    "<p>Built for the NeoBench bare-metal Amiga kernel.</p>"
    "<h2>Supported HTML Tags</h2>"
    "<ul>"
    "<li>Headings: h1-h6</li>"
    "<li>Text: p, br, hr, b, i, pre</li>"
    "<li>Lists: ul, ol, li</li>"
    "<li>Links: a (with href)</li>"
    "<li>Images: img (alt text only)</li>"
    "</ul>"
    "<p>Written in C++ for M68K. No libc required.</p>"
    "<p><a href=\"http://neobench.local/\">Back to Home</a></p>"
    "</body></html>"
};

int find_sample_page(const char* host, const char* path) {
    if (neo_strncmp(host, "neobench.local", 14) == 0) {
        if (neo_strcmp(path, "/") == 0 || neo_strcmp(path, "") == 0) return 0;
        if (neo_strncmp(path, "/help", 5) == 0) return 1;
        if (neo_strncmp(path, "/about", 6) == 0) return 2;
    }
    return -1;
}

// Generate a simple page for unknown hosts
void generate_placeholder(char* buf, int maxlen, const Url& u) {
    ksprintf(buf, maxlen,
        "<html><head><title>%s</title></head><body>"
        "<h1>%s</h1>"
        "<p>Connected to %s on port %d</p>"
        "<p>Requested path: %s</p>"
        "<hr>"
        "<p><i>Note: No actual network connection available. "
        "NeoNet TCP/IP stack required for real HTTP requests.</i></p>"
        "<p><a href=\"http://neobench.local/\">Go to NeoBrowse Home</a></p>"
        "</body></html>",
        u.host, u.host, u.host, u.port, u.path);
}

// --- History ---
struct HistoryEntry {
    Url url;
    bool valid;
};

static const int MAX_HISTORY = 32;
static HistoryEntry history[MAX_HISTORY];
static int history_count = 0;
static int history_pos = -1;

void history_push(const Url& u) {
    // Truncate forward history
    if (history_pos < history_count - 1) {
        history_count = history_pos + 1;
    }
    if (history_count < MAX_HISTORY) {
        history[history_count].url = u;
        history[history_count].valid = true;
        history_pos = history_count;
        history_count++;
    }
}

// --- Bookmarks ---
struct Bookmark {
    char title[64];
    char url[INODE_SIZE];
    bool valid;
};

static const int MAX_BOOKMARKS = 16;
static Bookmark bookmarks[MAX_BOOKMARKS];
static int bookmark_count = 0;

void bookmark_add(const char* title, const char* url_str) {
    if (bookmark_count < MAX_BOOKMARKS) {
        neo_strncpy(bookmarks[bookmark_count].title, title, 63);
        bookmarks[bookmark_count].title[63] = 0;
        neo_strncpy(bookmarks[bookmark_count].url, url_str, 255);
        bookmarks[bookmark_count].url[255] = 0;
        bookmarks[bookmark_count].valid = true;
        bookmark_count++;
    }
}

// --- Status bar ---
void draw_status(const char* title, const char* url_str) {
    int w = neo::display::get_width();
    neo::display::set_color(0, 3); // Dark on cyan
    neo::display::set_cursor(0, 0);
    neo::display::puts(" NeoBrowse | ");
    neo::display::puts(title);
    int used = 13 + neo_strlen(title);
    for (int i = used; i < w; i++) neo::display::putchar(' ');

    neo::display::set_cursor(0, 1);
    neo::display::set_color(15, 1); // White on blue
    neo::display::puts(" URL: ");
    neo::display::puts(url_str);
    used = 6 + neo_strlen(url_str);
    for (int i = used; i < w; i++) neo::display::putchar(' ');

    neo::display::set_color(7, 0);
    neo::display::set_cursor(0, 2);
}

void draw_bottom_bar() {
    int w = neo::display::get_width();
    int h = neo::display::get_height();
    neo::display::set_cursor(0, h - 1);
    neo::display::set_color(0, 3);
    neo::display::puts(" [g]o [b]ack [f]wd [s]rc [k]mark [m]arks [l]inks [h]ist [q]uit ");
    int used = 63;
    for (int i = used; i < w; i++) neo::display::putchar(' ');
    neo::display::set_color(7, 0);
}

} // namespace neobrowse

extern "C" void app_main(int argc, char** argv) {
    using namespace neobrowse;

    neo_memset(history, 0, sizeof(history));
    neo_memset(bookmarks, 0, sizeof(bookmarks));
    history_count = 0;
    history_pos = -1;
    bookmark_count = 0;
    link_count = 0;

    // Default bookmarks
    bookmark_add("NeoBrowse Home", "http://neobench.local/");
    bookmark_add("NeoBrowse Help", "http://neobench.local/help");

    // Current page
    Url current_url;
    url_parse("http://neobench.local/", current_url);
    char title[128] = "NeoBrowse Home";
    char url_str[512];
    char page_source[4096];
    int page_source_len = 0;
    bool view_source = false;

    // Load initial page
    auto load_page = [&]() {
        url_to_str(url_str, sizeof(url_str), current_url);

        int idx = find_sample_page(current_url.host, current_url.path);
        if (idx >= 0) {
            page_source_len = neo_strlen(sample_pages[idx]);
            if (page_source_len > (int)sizeof(page_source) - 1) page_source_len = sizeof(page_source) - 1;
            neo_memcpy(page_source, sample_pages[idx], page_source_len);
            page_source[page_source_len] = 0;
        } else {
            generate_placeholder(page_source, sizeof(page_source), current_url);
            page_source_len = neo_strlen(page_source);
        }

        neo::display::clear();
        draw_status(title, url_str);
        neo::display::set_cursor(0, 3);

        if (view_source) {
            neo::display::set_fg(10);
            for (int i = 0; i < page_source_len; i++) {
                neo::display::putchar(page_source[i]);
            }
            neo::display::set_fg(7);
        } else {
            render_html(page_source, page_source_len, title, sizeof(title));
        }

        // Update status with actual title
        draw_status(title, url_str);
        draw_bottom_bar();
    };

    history_push(current_url);
    load_page();

    char input[INODE_SIZE];
    bool running = true;

    while (running) {
        // Wait for key
        if (!neo::keyboard::key_available()) {
            neo::proc::yield();
            continue;
        }

        unsigned char sc = neo::keyboard::read_scancode();
        bool shift = neo::keyboard::is_shift_down();
        char ch = neo::keyboard::translate(sc, shift);

        if (ch == 'q' || ch == 'Q') {
            running = false;
        }
        else if (ch == 'g' || ch == 'G') {
            // Go to URL
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 2);
            neo::display::set_color(15, 0);
            neo::display::clear_eol();
            neo::console::getline(input, sizeof(input), "URL: ");
            if (input[0]) {
                // If no scheme, prepend http://
                char full_url[512];
                if (neo_strncmp(input, "http", 4) != 0) {
                    ksprintf(full_url, sizeof(full_url), "http://%s", input);
                } else {
                    neo_strncpy(full_url, input, 511);
                    full_url[511] = 0;
                }
                url_parse(full_url, current_url);
                history_push(current_url);
                view_source = false;
                load_page();
            }
        }
        else if (ch == 'b' || ch == 'B') {
            // Back
            if (history_pos > 0) {
                history_pos--;
                current_url = history[history_pos].url;
                view_source = false;
                load_page();
            }
        }
        else if (ch == 'f' || ch == 'F') {
            // Forward
            if (history_pos < history_count - 1) {
                history_pos++;
                current_url = history[history_pos].url;
                view_source = false;
                load_page();
            }
        }
        else if (ch == 'r' || ch == 'R') {
            load_page();
        }
        else if (ch == 's' || ch == 'S') {
            view_source = !view_source;
            load_page();
        }
        else if (ch == 'k' || ch == 'K') {
            url_to_str(url_str, sizeof(url_str), current_url);
            bookmark_add(title, url_str);
            int h = neo::display::get_height();
            neo::display::set_cursor(0, h - 2);
            neo::display::set_color(10, 0);
            neo::display::puts("Bookmark added!");
            neo::display::set_color(7, 0);
            neo::timer::delay_ms(1000);
            draw_bottom_bar();
        }
        else if (ch == 'm' || ch == 'M') {
            // Show bookmarks
            neo::display::clear();
            neo::display::set_color(14, 1);
            neo::display::puts("+============================+\n");
            neo::display::puts("|        Bookmarks           |\n");
            neo::display::puts("+============================+\n");
            neo::display::set_color(7, 0);
            for (int i = 0; i < bookmark_count; i++) {
                if (!bookmarks[i].valid) continue;
                neo::display::printf("  %d. %s\n     %s\n\n", i + 1,
                                     bookmarks[i].title, bookmarks[i].url);
            }
            neo::display::puts("\nEnter bookmark number (0 to cancel): ");
            neo::console::getline(input, sizeof(input), "");
            int sel = 0;
            for (int i = 0; input[i] >= '0' && input[i] <= '9'; i++)
                sel = sel * 10 + (input[i] - '0');
            if (sel >= 1 && sel <= bookmark_count) {
                url_parse(bookmarks[sel - 1].url, current_url);
                history_push(current_url);
                view_source = false;
            }
            load_page();
        }
        else if (ch == 'l' || ch == 'L') {
            // Show links on page
            int h = neo::display::get_height();
            neo::display::clear();
            neo::display::set_color(14, 0);
            neo::display::printf("Links on page (%d):\n\n", link_count);
            neo::display::set_color(7, 0);
            for (int i = 0; i < link_count; i++) {
                neo::display::set_fg(12);
                neo::display::printf("  [%d] ", i + 1);
                neo::display::set_fg(9);
                neo::display::printf("%s\n", page_links[i].url);
                if (page_links[i].text[0]) {
                    neo::display::set_fg(7);
                    neo::display::printf("      %s\n", page_links[i].text);
                }
            }
            neo::display::set_fg(7);
            neo::display::puts("\nEnter link number (0 to cancel): ");
            neo::console::getline(input, sizeof(input), "");
            int sel = 0;
            for (int i = 0; input[i] >= '0' && input[i] <= '9'; i++)
                sel = sel * 10 + (input[i] - '0');
            if (sel >= 1 && sel <= link_count) {
                char* lurl = page_links[sel - 1].url;
                if (neo_strncmp(lurl, "http", 4) == 0) {
                    url_parse(lurl, current_url);
                } else {
                    // Relative URL
                    neo_strncpy(current_url.path, lurl, 255);
                    current_url.path[255] = 0;
                }
                history_push(current_url);
                view_source = false;
            }
            load_page();
        }
        else if (ch == 'h' || ch == 'H') {
            // Show history
            neo::display::clear();
            neo::display::set_color(14, 1);
            neo::display::puts("+============================+\n");
            neo::display::puts("|        History             |\n");
            neo::display::puts("+============================+\n");
            neo::display::set_color(7, 0);
            for (int i = 0; i < history_count; i++) {
                char hurl[512];
                url_to_str(hurl, sizeof(hurl), history[i].url);
                neo::display::printf("  %s%d. %s\n",
                                     i == history_pos ? ">> " : "   ", i + 1, hurl);
            }
            neo::display::puts("\nPress any key to continue...");
            while (!neo::keyboard::key_available()) neo::proc::yield();
            neo::keyboard::read_scancode();
            load_page();
        }
        else if (ch >= '1' && ch <= '9') {
            // Quick link follow
            int linknum = ch - '0';
            if (linknum >= 1 && linknum <= link_count) {
                char* lurl = page_links[linknum - 1].url;
                if (neo_strncmp(lurl, "http", 4) == 0) {
                    url_parse(lurl, current_url);
                } else {
                    neo_strncpy(current_url.path, lurl, 255);
                    current_url.path[255] = 0;
                }
                history_push(current_url);
                view_source = false;
                load_page();
            }
        }
    }

    neo::display::clear();
    neo::display::set_color(7, 0);
    neo::display::puts("NeoBrowse session ended.\n");
}
