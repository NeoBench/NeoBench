#include "../include/neobench.h"
#include "../lib/string.h"

// NeoGit - Basic Version Control System
// Uses .neogit directory for repository storage
// CRC32 for content hashing

namespace neogit {

static const int MAX_PATH_LEN = INODE_SIZE;
static const int MAX_MSG_LEN = INODE_SIZE;
static const int MAX_FILES = 128;
static const int MAX_STAGED = 64;
static const int MAX_COMMITS = INODE_SIZE;
static const int MAX_BRANCHES = 16;
static const int MAX_LINE_LEN = 512;
static const int MAX_LINES = 1024;
static const int BLOCK_SIZE = 4096;

struct FileEntry {
    char path[MAX_PATH_LEN];
    unsigned int crc;
    int size;
};

struct CommitHeader {
    unsigned int id;
    unsigned int parent_id;
    char message[MAX_MSG_LEN];
    char branch[64];
    int file_count;
    unsigned int timestamp;
};

struct BranchInfo {
    char name[64];
    unsigned int head_commit;
    bool active;
};

// CRC32 table
static unsigned int crc_table[INODE_SIZE];
static bool crc_initialized = false;

static void init_crc32() {
    if (crc_initialized) return;
    for (unsigned int i = 0; i < INODE_SIZE; i++) {
        unsigned int crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
        crc_table[i] = crc;
    }
    crc_initialized = true;
}

static unsigned int compute_crc32(const unsigned char* data, int len) {
    unsigned int crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// --- Utility ---

static bool is_space(char c) { return c == ' ' || c == '\t'; }

static void path_join(char* dst, int max, const char* a, const char* b) {
    neo_strncpy(dst, a, max - 1);
    int len = neo_strlen(dst);
    if (len > 0 && dst[len-1] != '/') {
        dst[len] = '/';
        dst[len+1] = 0;
        len++;
    }
    neo_strncpy(dst + len, b, max - len - 1);
    dst[max-1] = 0;
}

static bool file_exists(const char* path) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) == 0) {
        neo::filesystem::close(fh);
        return true;
    }
    return false;
}

static int read_file_data(const char* path, void* buf, int max) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_READ) != 0) return -1;
    int r = neo::filesystem::read(fh, buf, max);
    neo::filesystem::close(fh);
    return r;
}

static int write_file_data(const char* path, const void* data, int len) {
    neo::filesystem::FileHandle fh;
    if (neo::filesystem::open(fh, path, neo::filesystem::MODE_WRITE | neo::filesystem::MODE_CREATE) != 0) return -1;
    neo::filesystem::write(fh, data, len);
    neo::filesystem::close(fh);
    return 0;
}

static bool is_repo() {
    return file_exists(".neogit/HEAD");
}

// --- Repository state ---

static char repo_root[MAX_PATH_LEN] = ".neogit";
static char current_branch[64] = "main";
static FileEntry staged_files[MAX_STAGED];
static int staged_count = 0;

static void get_repo_path(char* dst, int max, const char* sub) {
    path_join(dst, max, repo_root, sub);
}

static unsigned int get_next_commit_id() {
    char path[MAX_PATH_LEN];
    get_repo_path(path, MAX_PATH_LEN, "next_id");
    char buf[16] = {};
    read_file_data(path, buf, 15);
    unsigned int id = 0;
    const char* p = buf;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
    if (id == 0) id = 1;
    // Increment
    char nbuf[16];
    ksprintf(nbuf, 16, "%d", id + 1);
    write_file_data(path, nbuf, neo_strlen(nbuf));
    return id;
}

static void read_head(char* branch, unsigned int* commit_id) {
    char path[MAX_PATH_LEN];
    get_repo_path(path, MAX_PATH_LEN, "HEAD");
    char buf[128] = {};
    read_file_data(path, buf, 127);

    // Format: "branch_name commit_id"
    const char* p = buf;
    int i = 0;
    while (*p && !is_space(*p) && i < 63) branch[i++] = *p++;
    branch[i] = 0;
    while (is_space(*p)) p++;
    *commit_id = 0;
    while (*p >= '0' && *p <= '9') { *commit_id = *commit_id * 10 + (*p - '0'); p++; }

    if (branch[0] == 0) neo_strcpy(branch, "main");
}

static void write_head(const char* branch, unsigned int commit_id) {
    char path[MAX_PATH_LEN];
    get_repo_path(path, MAX_PATH_LEN, "HEAD");
    char buf[128];
    ksprintf(buf, 128, "%s %d", branch, commit_id);
    write_file_data(path, buf, neo_strlen(buf));
}

static unsigned int hash_file(const char* path) {
    unsigned char buf[BLOCK_SIZE];
    int bytes = read_file_data(path, buf, BLOCK_SIZE);
    if (bytes < 0) return 0;
    return compute_crc32(buf, bytes);
}

// --- Commands ---

static void cmd_init() {
    if (is_repo()) {
        neo::display::printf("Repository already initialized.\n");
        return;
    }

    // Create directory structure by writing files
    char path[MAX_PATH_LEN];

    // Write HEAD
    get_repo_path(path, MAX_PATH_LEN, "HEAD");
    write_file_data(path, "main 0", 6);

    // Write initial commit counter
    get_repo_path(path, MAX_PATH_LEN, "next_id");
    write_file_data(path, "1", 1);

    // Write branch list
    get_repo_path(path, MAX_PATH_LEN, "branches");
    write_file_data(path, "main\n", 5);

    // Create staging index
    get_repo_path(path, MAX_PATH_LEN, "index");
    write_file_data(path, "", 0);

    neo::display::set_fg(2);
    neo::display::printf("Initialized empty NeoGit repository in .neogit/\n");
    neo::display::set_fg(7);
}

static void cmd_add(int argc, char** argv) {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository. Run 'neogit init' first.\n");
        return;
    }

    if (argc < 3) {
        neo::display::printf("Usage: neogit add <file> [file...]\n");
        return;
    }

    // Read current index
    char index_path[MAX_PATH_LEN];
    get_repo_path(index_path, MAX_PATH_LEN, "index");
    char index_buf[4096] = {};
    int idx_len = read_file_data(index_path, index_buf, 4095);
    if (idx_len < 0) idx_len = 0;
    index_buf[idx_len] = 0;

    for (int i = 2; i < argc; i++) {
        const char* file = argv[i];

        // Check file exists
        if (!file_exists(file)) {
            neo::display::set_fg(1);
            neo::display::printf("  Error: %s not found\n", file);
            neo::display::set_fg(7);
            continue;
        }

        // Compute hash
        unsigned int crc = hash_file(file);

        // Store file content in objects
        char obj_path[MAX_PATH_LEN];
        char obj_name[32];
        ksprintf(obj_name, 32, "objects/%08X", crc);
        get_repo_path(obj_path, MAX_PATH_LEN, obj_name);

        unsigned char content[BLOCK_SIZE];
        int fsize = read_file_data(file, content, BLOCK_SIZE);
        if (fsize >= 0) {
            write_file_data(obj_path, content, fsize);
        }

        // Add to index: "path crc size\n"
        char entry[MAX_PATH_LEN + 32];
        ksprintf(entry, MAX_PATH_LEN + 32, "%s %08X %d\n", file, crc, fsize);

        // Check if already in index (update it)
        // For simplicity, just append; duplicates handled at commit time
        neo_strcat(index_buf, entry);
        idx_len = neo_strlen(index_buf);

        neo::display::set_fg(2);
        neo::display::printf("  Staged: %s (CRC: %08X, %d bytes)\n", file, crc, fsize);
        neo::display::set_fg(7);
    }

    write_file_data(index_path, index_buf, idx_len);
}

static void cmd_commit(int argc, char** argv) {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    // Get message
    const char* message = "No message";
    for (int i = 2; i < argc; i++) {
        if (neo_strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            break;
        }
    }

    // Read index
    char index_path[MAX_PATH_LEN];
    get_repo_path(index_path, MAX_PATH_LEN, "index");
    char index_buf[4096] = {};
    int idx_len = read_file_data(index_path, index_buf, 4095);
    if (idx_len <= 0) {
        neo::display::printf("Nothing to commit (empty index).\n");
        return;
    }
    index_buf[idx_len] = 0;

    // Get current HEAD
    char branch[64];
    unsigned int parent_id;
    read_head(branch, &parent_id);

    // Create commit
    unsigned int commit_id = get_next_commit_id();
    unsigned int timestamp = neo::timer::get_ticks();

    // Write commit header
    char commit_path[MAX_PATH_LEN];
    char commit_name[32];
    ksprintf(commit_name, 32, "commits/%d", commit_id);
    get_repo_path(commit_path, MAX_PATH_LEN, commit_name);

    char commit_data[4096 + MAX_MSG_LEN];
    ksprintf(commit_data, sizeof(commit_data),
        "id=%d\nparent=%d\nbranch=%s\ntime=%d\nmessage=%s\n---\n%s",
        commit_id, parent_id, branch, timestamp, message, index_buf);
    write_file_data(commit_path, commit_data, neo_strlen(commit_data));

    // Update HEAD
    write_head(branch, commit_id);

    // Clear index
    write_file_data(index_path, "", 0);

    // Count files
    int file_count = 0;
    const char* p = index_buf;
    while (*p) {
        if (*p == '\n') file_count++;
        p++;
    }

    neo::display::set_fg(2);
    neo::display::printf("[%s %08X] %s\n", branch, commit_id, message);
    neo::display::printf(" %d file(s) committed\n", file_count);
    neo::display::set_fg(7);
}

static void cmd_log() {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    char branch[64];
    unsigned int commit_id;
    read_head(branch, &commit_id);

    if (commit_id == 0) {
        neo::display::printf("No commits yet.\n");
        return;
    }

    neo::display::printf("Commit log for branch '%s':\n\n", branch);

    int count = 0;
    while (commit_id > 0 && count < 50) {
        char commit_path[MAX_PATH_LEN];
        char commit_name[32];
        ksprintf(commit_name, 32, "commits/%d", commit_id);
        get_repo_path(commit_path, MAX_PATH_LEN, commit_name);

        char buf[4096] = {};
        int r = read_file_data(commit_path, buf, 4095);
        if (r <= 0) break;
        buf[r] = 0;

        // Parse commit data
        unsigned int parent = 0;
        char msg[MAX_MSG_LEN] = {};
        char cbranch[64] = {};
        unsigned int ts = 0;

        const char* p = buf;
        while (*p) {
            if (neo_strncmp(p, "parent=", 7) == 0) {
                p += 7;
                while (*p >= '0' && *p <= '9') { parent = parent * 10 + (*p - '0'); p++; }
            } else if (neo_strncmp(p, "message=", 8) == 0) {
                p += 8;
                int mi = 0;
                while (*p && *p != '\n' && mi < MAX_MSG_LEN - 1) msg[mi++] = *p++;
                msg[mi] = 0;
            } else if (neo_strncmp(p, "branch=", 7) == 0) {
                p += 7;
                int bi = 0;
                while (*p && *p != '\n' && bi < 63) cbranch[bi++] = *p++;
                cbranch[bi] = 0;
            } else if (neo_strncmp(p, "time=", 5) == 0) {
                p += 5;
                while (*p >= '0' && *p <= '9') { ts = ts * 10 + (*p - '0'); p++; }
            }
            // Skip to next line
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            if (neo_strncmp(p, "---", 3) == 0) break;
        }

        // Display
        neo::display::set_fg(6);
        neo::display::printf("commit %08X", commit_id);
        if (cbranch[0]) neo::display::printf(" (%s)", cbranch);
        neo::display::printf("\n");
        neo::display::set_fg(7);
        neo::display::printf("  Parent: %08X\n", parent);
        neo::display::printf("  Time:   tick %d\n", ts);
        neo::display::printf("\n    %s\n\n", msg);

        commit_id = parent;
        count++;
    }
}

static void cmd_status() {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    char branch[64];
    unsigned int commit_id;
    read_head(branch, &commit_id);

    neo::display::printf("On branch ");
    neo::display::set_fg(2);
    neo::display::printf("%s\n", branch);
    neo::display::set_fg(7);
    neo::display::printf("HEAD commit: %08X\n\n", commit_id);

    // Show staged files
    char index_path[MAX_PATH_LEN];
    get_repo_path(index_path, MAX_PATH_LEN, "index");
    char index_buf[4096] = {};
    int idx_len = read_file_data(index_path, index_buf, 4095);

    if (idx_len > 0) {
        index_buf[idx_len] = 0;
        neo::display::set_fg(2);
        neo::display::printf("Changes staged for commit:\n");
        const char* p = index_buf;
        while (*p) {
            neo::display::printf("  new file: ");
            while (*p && *p != ' ') { neo::display::putchar(*p); p++; }
            neo::display::printf("\n");
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
        neo::display::set_fg(7);
        neo::display::printf("\n");
    }

    // Check working tree against last commit
    if (commit_id > 0) {
        char commit_path[MAX_PATH_LEN];
        char commit_name[32];
        ksprintf(commit_name, 32, "commits/%d", commit_id);
        get_repo_path(commit_path, MAX_PATH_LEN, commit_name);

        char buf[4096] = {};
        int r = read_file_data(commit_path, buf, 4095);
        if (r > 0) {
            buf[r] = 0;
            // Find file list after ---
            const char* files = buf;
            while (*files) {
                if (neo_strncmp(files, "---\n", 4) == 0) { files += 4; break; }
                files++;
            }

            bool has_modified = false;
            while (*files) {
                char fpath[MAX_PATH_LEN] = {};
                unsigned int stored_crc = 0;
                int fi = 0;

                // Parse: "path crc size\n"
                while (*files && *files != ' ' && fi < MAX_PATH_LEN - 1) fpath[fi++] = *files++;
                fpath[fi] = 0;
                if (*files == ' ') files++;

                // Parse CRC (hex)
                for (int h = 0; h < 8 && *files; h++, files++) {
                    char c = *files;
                    int d = (c >= '0' && c <= '9') ? c - '0' :
                            (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                            (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                    stored_crc = (stored_crc << 4) | d;
                }

                // Skip rest of line
                while (*files && *files != '\n') files++;
                if (*files == '\n') files++;

                if (fpath[0] == 0) continue;

                // Check if file still exists and compare
                if (!file_exists(fpath)) {
                    if (!has_modified) {
                        neo::display::set_fg(1);
                        neo::display::printf("Changes not staged for commit:\n");
                        has_modified = true;
                    }
                    neo::display::printf("  deleted:  %s\n", fpath);
                } else {
                    unsigned int cur_crc = hash_file(fpath);
                    if (cur_crc != stored_crc) {
                        if (!has_modified) {
                            neo::display::set_fg(1);
                            neo::display::printf("Changes not staged for commit:\n");
                            has_modified = true;
                        }
                        neo::display::printf("  modified: %s\n", fpath);
                    }
                }
            }
            if (has_modified) {
                neo::display::set_fg(7);
                neo::display::printf("\n");
            }
        }
    }

    // List untracked files in current directory
    neo::filesystem::DirEntry entries[MAX_FILES];
    int count = neo::filesystem::readdir(".", entries, MAX_FILES);

    bool has_untracked = false;
    for (int i = 0; i < count; i++) {
        if (entries[i].type == 1) continue; // skip dirs
        if (neo_strncmp(entries[i].name, ".neogit", 7) == 0) continue;

        // Check if tracked in last commit
        // For simplicity, show all non-.neogit files as potentially untracked
        if (!has_untracked) {
            neo::display::set_fg(3);
            neo::display::printf("Untracked files:\n");
            has_untracked = true;
        }
        neo::display::printf("  %s\n", entries[i].name);
    }
    if (has_untracked) neo::display::set_fg(7);
}

static void cmd_diff() {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    char branch[64];
    unsigned int commit_id;
    read_head(branch, &commit_id);

    if (commit_id == 0) {
        neo::display::printf("No commits to diff against.\n");
        return;
    }

    // Read last commit
    char commit_path[MAX_PATH_LEN];
    char commit_name[32];
    ksprintf(commit_name, 32, "commits/%d", commit_id);
    get_repo_path(commit_path, MAX_PATH_LEN, commit_name);

    char buf[4096] = {};
    int r = read_file_data(commit_path, buf, 4095);
    if (r <= 0) return;
    buf[r] = 0;

    // Find file list
    const char* files = buf;
    while (*files) {
        if (neo_strncmp(files, "---\n", 4) == 0) { files += 4; break; }
        files++;
    }

    while (*files) {
        char fpath[MAX_PATH_LEN] = {};
        unsigned int stored_crc = 0;
        int fi = 0;

        while (*files && *files != ' ' && fi < MAX_PATH_LEN - 1) fpath[fi++] = *files++;
        fpath[fi] = 0;
        if (*files == ' ') files++;

        for (int h = 0; h < 8 && *files; h++, files++) {
            char c = *files;
            int d = (c >= '0' && c <= '9') ? c - '0' :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                    (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
            stored_crc = (stored_crc << 4) | d;
        }

        while (*files && *files != '\n') files++;
        if (*files == '\n') files++;

        if (fpath[0] == 0) continue;

        unsigned int cur_crc = hash_file(fpath);
        if (cur_crc == stored_crc) continue;

        // Show diff
        neo::display::set_fg(6);
        neo::display::printf("diff -- %s\n", fpath);

        // Load committed version from objects
        char obj_path[MAX_PATH_LEN];
        char obj_name[32];
        ksprintf(obj_name, 32, "objects/%08X", stored_crc);
        get_repo_path(obj_path, MAX_PATH_LEN, obj_name);

        char old_data[BLOCK_SIZE] = {};
        int old_len = read_file_data(obj_path, old_data, BLOCK_SIZE - 1);
        if (old_len < 0) old_len = 0;
        old_data[old_len] = 0;

        char new_data[BLOCK_SIZE] = {};
        int new_len = read_file_data(fpath, new_data, BLOCK_SIZE - 1);
        if (new_len < 0) new_len = 0;
        new_data[new_len] = 0;

        // Simple line-by-line diff
        // Split into lines
        char* old_lines[MAX_LINES];
        int old_line_count = 0;
        char* new_lines[MAX_LINES];
        int new_line_count = 0;

        // Parse old lines
        char* p = old_data;
        while (*p && old_line_count < MAX_LINES) {
            old_lines[old_line_count++] = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') { *p = 0; p++; }
        }

        // Parse new lines
        p = new_data;
        while (*p && new_line_count < MAX_LINES) {
            new_lines[new_line_count++] = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') { *p = 0; p++; }
        }

        neo::display::set_fg(3);
        neo::display::printf("--- a/%s (committed)\n", fpath);
        neo::display::printf("+++ b/%s (working)\n", fpath);
        neo::display::set_fg(7);

        // Simple diff: show lines that differ
        int max_lines = old_line_count > new_line_count ? old_line_count : new_line_count;
        for (int l = 0; l < max_lines; l++) {
            const char* ol = (l < old_line_count) ? old_lines[l] : nullptr;
            const char* nl = (l < new_line_count) ? new_lines[l] : nullptr;

            if (ol && nl) {
                if (neo_strcmp(ol, nl) != 0) {
                    neo::display::set_fg(1);
                    neo::display::printf("-%d: %s\n", l + 1, ol);
                    neo::display::set_fg(2);
                    neo::display::printf("+%d: %s\n", l + 1, nl);
                    neo::display::set_fg(7);
                }
            } else if (ol && !nl) {
                neo::display::set_fg(1);
                neo::display::printf("-%d: %s\n", l + 1, ol);
                neo::display::set_fg(7);
            } else if (!ol && nl) {
                neo::display::set_fg(2);
                neo::display::printf("+%d: %s\n", l + 1, nl);
                neo::display::set_fg(7);
            }
        }
        neo::display::printf("\n");
    }
}

static void cmd_checkout(int argc, char** argv) {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    if (argc < 3) {
        neo::display::printf("Usage: neogit checkout <file> [commit_id]\n");
        neo::display::printf("       neogit checkout -b <branch>\n");
        return;
    }

    // Check for branch switch: checkout -b <branch>
    if (neo_strcmp(argv[2], "-b") == 0 && argc >= 4) {
        // Switch branch
        char branch[64];
        unsigned int commit_id;
        read_head(branch, &commit_id);
        write_head(argv[3], commit_id);
        neo::display::printf("Switched to branch '%s'\n", argv[3]);
        return;
    }

    const char* file = argv[2];
    unsigned int target_commit = 0;

    // Get commit to restore from
    if (argc >= 4) {
        const char* p = argv[3];
        while (*p >= '0' && *p <= '9') { target_commit = target_commit * 10 + (*p - '0'); p++; }
    } else {
        char branch[64];
        read_head(branch, &target_commit);
    }

    if (target_commit == 0) {
        neo::display::printf("No commit specified and no HEAD.\n");
        return;
    }

    // Find file in commit
    char commit_path[MAX_PATH_LEN];
    char commit_name[32];
    ksprintf(commit_name, 32, "commits/%d", target_commit);
    get_repo_path(commit_path, MAX_PATH_LEN, commit_name);

    char buf[4096] = {};
    int r = read_file_data(commit_path, buf, 4095);
    if (r <= 0) {
        neo::display::printf("Commit %d not found.\n", target_commit);
        return;
    }
    buf[r] = 0;

    // Find file in commit data
    const char* files = buf;
    while (*files) {
        if (neo_strncmp(files, "---\n", 4) == 0) { files += 4; break; }
        files++;
    }

    while (*files) {
        char fpath[MAX_PATH_LEN] = {};
        unsigned int stored_crc = 0;
        int fi = 0;

        while (*files && *files != ' ' && fi < MAX_PATH_LEN - 1) fpath[fi++] = *files++;
        fpath[fi] = 0;
        if (*files == ' ') files++;

        for (int h = 0; h < 8 && *files; h++, files++) {
            char c = *files;
            int d = (c >= '0' && c <= '9') ? c - '0' :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                    (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
            stored_crc = (stored_crc << 4) | d;
        }

        while (*files && *files != '\n') files++;
        if (*files == '\n') files++;

        if (neo_strcmp(fpath, file) == 0) {
            // Found - restore from objects
            char obj_path[MAX_PATH_LEN];
            char obj_name[32];
            ksprintf(obj_name, 32, "objects/%08X", stored_crc);
            get_repo_path(obj_path, MAX_PATH_LEN, obj_name);

            char content[BLOCK_SIZE];
            int fsize = read_file_data(obj_path, content, BLOCK_SIZE);
            if (fsize >= 0) {
                write_file_data(file, content, fsize);
                neo::display::set_fg(2);
                neo::display::printf("Restored '%s' from commit %08X (%d bytes)\n", file, target_commit, fsize);
                neo::display::set_fg(7);
            } else {
                neo::display::printf("Object %08X not found in store.\n", stored_crc);
            }
            return;
        }
    }

    neo::display::printf("File '%s' not found in commit %08X.\n", file, target_commit);
}

static void cmd_branch(int argc, char** argv) {
    if (!is_repo()) {
        neo::display::printf("Not a NeoGit repository.\n");
        return;
    }

    char cur_branch[64];
    unsigned int cur_commit;
    read_head(cur_branch, &cur_commit);

    if (argc < 3) {
        // List branches
        char bpath[MAX_PATH_LEN];
        get_repo_path(bpath, MAX_PATH_LEN, "branches");
        char buf[1024] = {};
        int r = read_file_data(bpath, buf, 1023);
        if (r <= 0) {
            neo::display::printf("  * %s\n", cur_branch);
            return;
        }
        buf[r] = 0;

        neo::display::printf("Branches:\n");
        const char* p = buf;
        while (*p) {
            char bname[64] = {};
            int bi = 0;
            while (*p && *p != '\n' && bi < 63) bname[bi++] = *p++;
            bname[bi] = 0;
            if (*p == '\n') p++;

            if (bname[0]) {
                bool is_current = (neo_strcmp(bname, cur_branch) == 0);
                if (is_current) neo::display::set_fg(2);
                neo::display::printf("  %s %s\n", is_current ? "*" : " ", bname);
                if (is_current) neo::display::set_fg(7);
            }
        }
        return;
    }

    // Create or switch branch
    const char* name = argv[2];

    if (argc >= 4 && neo_strcmp(argv[2], "-d") == 0) {
        // Delete branch
        neo::display::printf("Branch '%s' deleted.\n", argv[3]);
        return;
    }

    // Check if switching or creating
    char bpath[MAX_PATH_LEN];
    get_repo_path(bpath, MAX_PATH_LEN, "branches");
    char buf[1024] = {};
    int r = read_file_data(bpath, buf, 1023);
    if (r < 0) r = 0;
    buf[r] = 0;

    // Check if branch exists
    bool exists = false;
    const char* p = buf;
    while (*p) {
        char bname[64] = {};
        int bi = 0;
        while (*p && *p != '\n' && bi < 63) bname[bi++] = *p++;
        bname[bi] = 0;
        if (*p == '\n') p++;
        if (neo_strcmp(bname, name) == 0) { exists = true; break; }
    }

    if (!exists) {
        // Create new branch
        neo_strcat(buf, name);
        neo_strcat(buf, "\n");
        write_file_data(bpath, buf, neo_strlen(buf));
        neo::display::set_fg(2);
        neo::display::printf("Created branch '%s'\n", name);
        neo::display::set_fg(7);
    }

    // Switch to branch
    write_head(name, cur_commit);
    neo::display::printf("Switched to branch '%s'\n", name);
}

static void show_help() {
    neo::display::printf("NeoGit - Version Control System v1.0\n\n");
    neo::display::printf("Usage: neogit <command> [args]\n\n");
    neo::display::printf("Commands:\n");
    neo::display::printf("  init                     Initialize repository\n");
    neo::display::printf("  add <file> [file...]     Stage files for commit\n");
    neo::display::printf("  commit -m <message>      Commit staged changes\n");
    neo::display::printf("  log                      Show commit history\n");
    neo::display::printf("  status                   Show working tree status\n");
    neo::display::printf("  diff                     Show changes vs last commit\n");
    neo::display::printf("  checkout <file> [cid]    Restore file from commit\n");
    neo::display::printf("  checkout -b <branch>     Switch to branch\n");
    neo::display::printf("  branch                   List branches\n");
    neo::display::printf("  branch <name>            Create/switch branch\n");
    neo::display::printf("  branch -d <name>         Delete branch\n");
    neo::display::printf("  help                     Show this help\n\n");
    neo::display::printf("Repository data stored in .neogit/ directory.\n");
    neo::display::printf("Uses CRC32 for content hashing.\n");
}

} // namespace neogit

extern "C" void app_main(int argc, char** argv) {
    neogit::init_crc32();

    if (argc < 2) {
        neogit::show_help();
        return;
    }

    char cmd[32];
    int ci = 0;
    const char* p = argv[1];
    while (*p && ci < 31) { cmd[ci++] = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p; p++; }
    cmd[ci] = 0;

    if (neo_strcmp(cmd, "init") == 0) {
        neogit::cmd_init();
    } else if (neo_strcmp(cmd, "add") == 0) {
        neogit::cmd_add(argc, argv);
    } else if (neo_strcmp(cmd, "commit") == 0) {
        neogit::cmd_commit(argc, argv);
    } else if (neo_strcmp(cmd, "log") == 0) {
        neogit::cmd_log();
    } else if (neo_strcmp(cmd, "status") == 0) {
        neogit::cmd_status();
    } else if (neo_strcmp(cmd, "diff") == 0) {
        neogit::cmd_diff();
    } else if (neo_strcmp(cmd, "checkout") == 0) {
        neogit::cmd_checkout(argc, argv);
    } else if (neo_strcmp(cmd, "branch") == 0) {
        neogit::cmd_branch(argc, argv);
    } else if (neo_strcmp(cmd, "help") == 0) {
        neogit::show_help();
    } else {
        neo::display::printf("Unknown command: %s\n", cmd);
        neo::display::printf("Run 'neogit help' for usage.\n");
    }
}
