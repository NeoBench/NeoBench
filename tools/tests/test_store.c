/*
 * Host test of the store: the Rust half held against the C.
 *
 * boot/rom/pfs.c has walked this store since there was one, and
 * user/gui/desktop/progs.c -- moved out of main.c in phase 2 -- holds
 * the other two pieces the phase reads: prog_name[], the list `progs`
 * and `run` ask, and program_slot_of(), the first-line rule that says
 * whether a file in the store is a program.  libs/nb_rs/src/store.rs
 * answers all of them the way the C answers them, and this test hands
 * both halves the same table through the same batteries the kernel
 * passes at boot: every node's path and a set of edge queries against
 * pfs_find, one children segment per node, program_slot_of for every
 * node, a `run` battery of arguments, and an exhaustive sweep of
 * every (directory, after) pair against pfs_next_child -- which the
 * boot battery samples rather than sweeps, four `after' values a walk
 * would never stand on, because the kernel has no heap and this test
 * has all of it.  Any battery coming back marked fails.
 *
 * Agreement alone would pass two walkers that were both wrong, so the
 * battery after it pins what the answers must actually be, out of the
 * C by strcmp and by its own indexes: "/Config" is the root (the code
 * says so, whatever its comment says), Config/Preferences is slot 4,
 * desktop.txt is a document, Apps leads root's children, and `run`
 * with "CLOCK" lands on clock.  And the last cases are the negative
 * control: the C's answers are deliberately falsified, one battery at
 * a time, and the mask has to name the family that moved with `at`
 * pointing at the entry -- plus both together, because a first-
 * disagreement word that only works when one thing is wrong is a
 * different word than the boot reads.  A check that cannot fail is
 * not a check.
 *
 *   cc -O2 -Wall -Wextra -std=c99 -o test_store test_store.c \
 *       ../../user/gui/desktop/progs.c ../../boot/rom/pfs.c \
 *       ../../boot/rom/pfs_data.c \
 *       ../../libs/nb_rs/target/release/libnb_rs.a \
 *       -lpthread -ldl -lm
 *   ./test_store
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../boot/rom/pfs.h"
#include "../../user/gui/desktop/progs.h"

/*
 * The host's view of struct pfs_node: four bytes a field with no
 * padding, the same as the ROM's, but a pointer is eight here and
 * four there -- so `parent` lands at 16 with padding behind it, and
 * the struct is 40 bytes rather than 24.  rustc asserts both pairs
 * against itself in store.rs, gcc's m68k pair against itself in
 * kernel_init, and the number this half computed meets gcc's own
 * sizeof below at run time through nb_rs_store_size().
 */
_Static_assert(offsetof(struct pfs_node, path) == 8, "layout moved");
_Static_assert(offsetof(struct pfs_node, parent) == 16, "layout moved");
_Static_assert(offsetof(struct pfs_node, data) == 24, "layout moved");
_Static_assert(offsetof(struct pfs_node, size) == 32, "layout moved");
_Static_assert(offsetof(struct pfs_node, dir) == 36, "layout moved");
_Static_assert(sizeof(struct pfs_node) == 40, "size moved");

/* The Rust half, as the kernel declares it (kernel_init). */
extern unsigned nb_rs_store_check(const struct pfs_node *nodes,
                                  unsigned ncount,
                                  const char *const *queries, unsigned nq,
                                  const unsigned *hits,
                                  const unsigned *seq, unsigned nseq,
                                  const char *const *names, unsigned nnames,
                                  const unsigned *slots, unsigned nslots,
                                  const char *const *args,
                                  const unsigned *alens, unsigned nargs,
                                  const unsigned *want,
                                  const unsigned *nxt,
                                  const unsigned *nxt_hit, unsigned nnxt,
                                  unsigned *at);
extern unsigned nb_rs_store_size(void);
extern unsigned nb_rs_selftest(unsigned seed);

/* The family bits, as store.rs spells them. */
#define F_PATH  0x00000001u
#define F_WALK  0x00000002u
#define F_NAME  0x00000004u
#define F_SLOT  0x00000008u
#define F_RUN   0x00000010u
#define F_NEXT  0x00000020u
#define BAD_CALL 0x80000000u

/* The batteries are sized for a store this test refuses to exceed
 * rather than one it silently checks in part of -- 128 is the same
 * number rs_store() in kernel_init uses, and the ROM ships a
 * fraction of it. */
#define NMAX   128
#define QEDGE   32
#define ARMAX   64

static int failures;

static void ok(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* The node whose path says so, or -1 -- strcmp over the table, not
 * pfs_find, so the pins do not lean on the code they pin. */
static int idx_of(const char *path)
{
    unsigned i;

    for (i = 0; i < nb_pfs_count; i++)
        if (nb_pfs_nodes[i].path && strcmp(nb_pfs_nodes[i].path, path) == 0)
            return (int)i;
    return -1;
}

/* ------------------------------------------------------------------ *
 * The batteries, built the way kernel_init's rs_store builds them
 * ------------------------------------------------------------------ */

static const char *queries[NMAX + QEDGE];
static unsigned    hits[NMAX + QEDGE];
static unsigned    seq[3 * NMAX];
static unsigned    slots[NMAX];
static const char *av[ARMAX];
static unsigned    alens[ARMAX];
static unsigned    want[ARMAX];
static unsigned    nxt[NMAX * (NMAX + 1)];
static unsigned    nxt_hit[NMAX * (NMAX + 1)];

static unsigned nq, nseq, nargs, nnxt;

/* The edge paths: the two special first bytes, a NULL, and every
 * miss worth spelling -- case, trailing slash, dot segments, the tab
 * that is a word rather than a slash. */
static const char *const edge[] = {
    NULL, "", "/", "/Config", "/Core/Docs/About", "/Config/screen.cfg",
    "Config/", "config/screen.cfg", "Config/screen.cfg/",
    "No/such/file", "Tools/Clock/", "Core/Docs/About.txt",
    "Core/Docs/About ", "Temp/../Temp", "\tConfig", "Core",
    "Core/Docs", "Home/Music/arpeggio.wav", "\x01Config",
    "Config/screen.cfg\x01"
};

/* What `run <arg>` is asked, beside the eight from the table. */
static const char *const rv[] = {
    "Files", "FILES", "file", "files2", "clock", "Clock",
    " clock", "clock ", "", "prefer", "PREFERENCES", "about.txt",
    "0clock", "neotext", "NeoText", "NEOSHELL", "vlc", "VLC",
    "monitor", "MONITOR", "Monitor", "NeoShell", "preference",
    "preferencesx", "pro gram", "cloc k", "Preferences", "ABOUT",
    "  neoshell", "neoshell ", "\tvlc"
};

static void build(void)
{
    unsigned i, d, c;

    nq = 0;
    nseq = 0;
    nargs = 0;
    nnxt = 0;

    for (i = 0; i < nb_pfs_count; i++)
    {
        const struct pfs_node *n;

        queries[nq] = nb_pfs_nodes[i].path;
        n = pfs_find(queries[nq]);
        hits[nq] = n ? (unsigned)(n - nb_pfs_nodes) : PFS_NONE;
        nq++;
    }
    for (i = 0; i < sizeof edge / sizeof edge[0]; i++)
    {
        const struct pfs_node *n;

        queries[nq] = edge[i];
        n = pfs_find(edge[i]);
        hits[nq] = n ? (unsigned)(n - nb_pfs_nodes) : PFS_NONE;
        nq++;
    }

    for (d = 0; d < nb_pfs_count; d++)
    {
        seq[nseq++] = d;
        c = pfs_first_child(d);
        while (c != PFS_NONE)
        {
            seq[nseq++] = c;
            c = pfs_next_child(d, c);
        }
        seq[nseq++] = PFS_NONE;
    }

    for (i = 0; i < nb_pfs_count; i++)
        slots[i] = (unsigned)program_slot_of(i);

    for (i = 0; i < N_PROGRAMS; i++)
        av[nargs++] = prog_name[i];
    for (i = 0; i < sizeof rv / sizeof rv[0] && nargs < ARMAX; i++)
        av[nargs++] = rv[i];
    for (i = 0; i < nargs; i++)
    {
        const unsigned char *a = (const unsigned char *)av[i];
        const unsigned char *e = a;

        while (*e)
            e++;
        alens[i] = (unsigned)(e - a);
        want[i] = PFS_NONE;
        for (d = 0; d < N_PROGRAMS; d++)
            if (tok_is(a, e, prog_name[d]))
            {
                want[i] = d;
                break;
            }
    }

    /* Exhaustive where the boot samples: every node as the directory
     * and every `after' the table can hold, the wrap included.  The
     * pairs are (d, after) laid down one after the other. */
    for (d = 0; d < nb_pfs_count; d++)
    {
        unsigned k;

        for (k = 0; k < nb_pfs_count; k++)
        {
            nxt[nnxt * 2]     = d;
            nxt[nnxt * 2 + 1] = k;
            nxt_hit[nnxt]     = pfs_next_child(d, k);
            nnxt++;
        }
        nxt[nnxt * 2]     = d;
        nxt[nnxt * 2 + 1] = PFS_NONE;
        nxt_hit[nnxt]     = pfs_next_child(d, PFS_NONE);
        nnxt++;
    }
}

/* Exactly kernel_init's call, over whatever has been built. */
static unsigned check(const char *const *nm, unsigned nnm,
                      const unsigned *sl, const unsigned *wt,
                      const unsigned *nh, unsigned *at)
{
    return nb_rs_store_check(nb_pfs_nodes, nb_pfs_count,
                             queries, nq, hits,
                             seq, nseq,
                             nm, nnm,
                             sl, nb_pfs_count,
                             av, alens, nargs, wt,
                             nxt, nh, nnxt, at);
}

static unsigned agree(unsigned *at)
{
    return check(prog_name, N_PROGRAMS, slots, want, nxt_hit, at);
}

/* ------------------------------------------------------------------ *
 * What the answers have to be, whatever Rust says
 * ------------------------------------------------------------------ */

static void pins(void)
{
    int screen = idx_of("Config/screen.cfg");
    int prefs  = idx_of("Config/Preferences");
    int apps   = idx_of("Apps");
    int core   = idx_of("Core");
    int home   = idx_of("Home");
    unsigned i;

    ok(nb_rs_store_size() == sizeof(struct pfs_node),
       "rustc's struct pfs_node is gcc's sizeof");

    /* pfs_find: exact, case-sensitive, and the first byte decides
     * the root -- "/Config" is the root, comment notwithstanding. */
    ok(pfs_find(NULL) == NULL, "a NULL query is a miss");
    ok(pfs_find("") == &nb_pfs_nodes[PFS_ROOT], "the empty query is the root");
    ok(pfs_find("/") == &nb_pfs_nodes[PFS_ROOT], "the slash is the root");
    ok(pfs_find("/Config") == &nb_pfs_nodes[PFS_ROOT],
       "a leading slash is the root, not the directory");
    ok(screen >= 0 && pfs_find("Config/screen.cfg") == &nb_pfs_nodes[screen],
       "Config/screen.cfg finds itself");
    ok(pfs_find("config/screen.cfg") == NULL,
       "the store does not fold case");
    ok(pfs_find("Config/screen.cfgg") == NULL, "no prefix matching");
    ok(pfs_find("Config/screen.cfg/") == NULL, "no trailing slash");

    /* The walk: Apps leads root's children, and Core's next sibling
     * is Home -- mkpfs sorts them that way. */
    ok(apps >= 0 && pfs_first_child(PFS_ROOT) == (unsigned)apps,
       "Apps leads the root's children");
    ok(core >= 0 && home >= 0 &&
       pfs_next_child(PFS_ROOT, (unsigned)core) == (unsigned)home,
       "Home follows Core at the root");
    ok(pfs_next_child(PFS_ROOT, PFS_NONE) == (unsigned)apps,
       "after the wrap the walk starts at the top");

    /* The table `progs` and `run` read, by name and by slot. */
    ok(prog_name[0] && strcmp(prog_name[0], "files") == 0, "slot 0 is files");
    ok(prog_name[4] && strcmp(prog_name[4], "preferences") == 0,
       "slot 4 is preferences");
    ok(prog_name[7] && strcmp(prog_name[7], "neoshell") == 0,
       "slot 7 is neoshell");
    ok(N_PROGRAMS == 8, "eight slots");
    ok(prefs >= 0 && program_slot_of((unsigned)prefs) == 4,
       "Config/Preferences is a program, slot 4");
    ok(idx_of("Tools/Clock") >= 0 &&
       program_slot_of((unsigned)idx_of("Tools/Clock")) == 1,
       "Tools/Clock is slot 1");
    ok(idx_of("Core/Media/VLC") >= 0 &&
       program_slot_of((unsigned)idx_of("Core/Media/VLC")) == 6,
       "Core/Media/VLC is slot 6");
    ok(idx_of("Home/Desktop/desktop.txt") >= 0 &&
       program_slot_of((unsigned)idx_of("Home/Desktop/desktop.txt")) == -1,
       "a document is -1");
    ok(idx_of("Config/boot.cfg") >= 0 &&
       program_slot_of((unsigned)idx_of("Config/boot.cfg")) == -1,
       "a config file that names no program is a document");

    /* The run battery's answers, out of C's tok_is over C's table. */
    for (i = 0; i < nargs; i++)
    {
        if (strcmp(av[i], "clock") == 0)
            ok(want[i] == 1, "`run clock` lands on clock");
        if (strcmp(av[i], "CLOCK") == 0)
            ok(want[i] == 1, "`run CLOCK` folds to clock");
        if (strcmp(av[i], "file") == 0)
            ok(want[i] == PFS_NONE, "`run file` is not files");
        if (strcmp(av[i], "NeoShell") == 0)
            ok(want[i] == 7, "`run NeoShell` folds to neoshell");
    }

    /* The batteries are as big as the table says they are. */
    ok(nq == nb_pfs_count + sizeof edge / sizeof edge[0],
       "every path and every edge was asked");
    ok(nseq >= 3 * nb_pfs_count - 1 && nseq <= 3 * nb_pfs_count,
       "one walk segment per node");
    ok(nnxt == nb_pfs_count * (nb_pfs_count + 1),
       "every (directory, after) pair was sampled");
}

/* ------------------------------------------------------------------ *
 * Both halves, one door
 * ------------------------------------------------------------------ */

int main(void)
{
    unsigned at = 0xDEADBEEFu, m;

    if (nb_pfs_count == 0 || nb_pfs_count > NMAX)
    {
        printf("FAIL: the store has %u nodes; NMAX is %d\n",
               nb_pfs_count, NMAX);
        return 1;
    }

    build();
    pins();

    /* The happy call: every battery, both halves, nothing marked. */
    m = agree(&at);
    if (m)
    {
        printf("FAIL: halves disagree %08x at %08x\n", m, at);
        failures++;
    }
    ok(at == 0, "a passing call writes nothing to `at`");

    /* The negative controls: the C's answers are falsified one
     * battery at a time, and the mask has to name the family with
     * `at` on the entry that moved. */
    {
        hits[0] = 7;                        /* node 0's own path, wrong */
        m = agree(&at);
        ok(m == F_PATH && at == 0,
           "a falsified path answer names paths at that query");
        build();                            /* the truth, restored */
    }
    {
        unsigned at2 = 0;
        unsigned save = seq[1];

        seq[1] = save ^ 0x5A5A5A5Au;
        m = agree(&at2);
        ok(m == F_WALK && at2 == 1,
           "a falsified child names the walk at that position");
        seq[1] = save;
    }
    {
        unsigned at2 = 0;
        static const char *bad[N_PROGRAMS];
        unsigned i;

        for (i = 0; i < N_PROGRAMS; i++)
            bad[i] = prog_name[i];
        bad[1] = "clocks";
        m = check(bad, N_PROGRAMS, slots, want, nxt_hit, &at2);
        ok(m == F_NAME && at2 == 1,
           "a falsified table entry names the table at that slot");
    }
    {
        unsigned at2 = 0;
        unsigned save = slots[5];

        slots[5] = save ^ 0x00000001u;
        m = agree(&at2);
        ok(m == F_SLOT && at2 == 5,
           "a falsified slot names the parser at that node");
        slots[5] = save;
    }
    {
        unsigned at2 = 0;
        unsigned save = want[0];

        want[0] = 6;                        /* `files` claimed as vlc */
        m = agree(&at2);
        ok(m == F_RUN && at2 == 0,
           "a falsified run answer names the run battery at that argument");
        want[0] = save;
    }
    {
        unsigned at2 = 0;
        unsigned save = nxt_hit[3];

        nxt_hit[3] = save ^ 0x80000000u;
        m = agree(&at2);
        ok(m == F_NEXT && at2 == 3,
           "a falsified next answer names next at that pair");
        nxt_hit[3] = save;
    }
    {
        /* Two families at once: `at` is the first in the order the
         * parameters come, not the smaller number and not the last --
         * so the path at query 5 is what `at` names, even though the
         * run answer at argument 0 is the smaller index. */
        unsigned at2 = 0;
        unsigned save_h = hits[5];
        unsigned save_w = want[0];

        hits[5] = 6;
        want[0] = 6;
        m = agree(&at2);
        ok(m == (F_PATH | F_RUN) && at2 == 5,
           "two families come back together and `at` is the first");
        hits[5] = save_h;
        want[0] = save_w;
    }

    /* And the call itself, when there is nothing to hold against. */
    {
        unsigned at2 = 0;
        unsigned sl0 = 0;

        m = nb_rs_store_check(NULL, nb_pfs_count, queries, nq, hits,
                              seq, nseq, prog_name, N_PROGRAMS,
                              slots, nb_pfs_count, av, alens, nargs,
                              want, nxt, nxt_hit, nnxt, &at2);
        ok(m == BAD_CALL, "no table is its own answer");

        m = nb_rs_store_check(nb_pfs_nodes, nb_pfs_count, queries, nq,
                              hits, seq, nseq, prog_name, N_PROGRAMS,
                              &sl0, nb_pfs_count - 1, av, alens, nargs,
                              want, nxt, nxt_hit, nnxt, &at2);
        ok(m == BAD_CALL, "a slot battery of the wrong length cannot stand");

        m = nb_rs_store_check(nb_pfs_nodes, 0, queries, nq, hits,
                              seq, nseq, prog_name, N_PROGRAMS,
                              slots, 0, av, alens, nargs, want,
                              nxt, nxt_hit, nnxt, &at2);
        ok(m == BAD_CALL, "an empty table cannot stand");

        m = nb_rs_store_check(nb_pfs_nodes, nb_pfs_count, queries, 1,
                              hits, seq, nseq, prog_name, N_PROGRAMS,
                              slots, nb_pfs_count, av, alens, nargs,
                              want, nxt, nxt_hit, nnxt, &at2);
        ok(m == BAD_CALL, "a paths battery shorter than the table cannot stand");

        m = nb_rs_store_check(nb_pfs_nodes, nb_pfs_count, queries, nq,
                              hits, seq, nseq, prog_name, N_PROGRAMS,
                              slots, nb_pfs_count, av, alens, 0,
                              want, nxt, nxt_hit, nnxt, &at2);
        ok(m == BAD_CALL, "no arguments at all cannot stand");

        m = nb_rs_store_check(nb_pfs_nodes, nb_pfs_count, queries, nq,
                              hits, seq, nseq, prog_name, N_PROGRAMS,
                              slots, nb_pfs_count, av, alens, nargs,
                              want, nxt, nxt_hit, nnxt, NULL);
        ok(m == BAD_CALL, "nowhere to write `at` cannot stand");
    }

    /* The boot selftest, run here too -- see test_prefs for what the
     * legs are for. */
    ok(nb_rs_selftest(0x12345678u) == 0, "the boot selftest holds for one seed");
    ok(nb_rs_selftest(0x9e3779b9u) == 0, "and for another");

    if (failures)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_store: all checks passed\n");
    return 0;
}
