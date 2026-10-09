/*
 * The program table and the parsing that fills it from the store.
 *
 * tok_is(), prog_name[] and program_slot_of() moved here verbatim from
 * main.c (where they were static) so that the parser can be held
 * against its Rust twin -- tools/tests/test_store.c on the host and
 * rs_store() at boot both link this file rather than a copy of it.
 * The behaviour is main.c's, down to which line of a file gets to
 * decide and what a typo costs: see progs.h for the contract.
 */
#include "progs.h"
#include "../../../boot/rom/pfs.h"

/* one word of a file, compared without regard to case */
int tok_is(const unsigned char *s, const unsigned char *e,
           const char *w)
{
    while (s < e && *w)
    {
        unsigned char a = *s++;
        unsigned char b = (unsigned char)*w++;

        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return s == e && !*w;
}

const char *const prog_name[N_PROGRAMS] = {
    "files", "clock", "monitor", "about", "preferences", "neotext",
    "vlc", "neoshell"
};

/*
 * Is this file a program rather than a document?
 *
 * The first line of a file that is neither blank nor a comment may name
 * what the file is for -- "program = preferences" -- and a file that
 * does name one starts that program when it is chosen in the browser
 * instead of opening in NeoText.  That is how a drawer holds programs
 * the way the start menu holds them: Config/ carries the Preferences
 * entry beside the four files it reads at boot, and any other directory
 * can carry whatever the desktop can run.
 *
 * A file that says nothing, or that names a program the desktop does
 * not have, is a document and opens in the reader as it always has, so
 * a typo costs the shortcut and nothing else.  Answers the start
 * menu's program slot, or -1 for a document.
 */
int program_slot_of(unsigned t)
{
    const unsigned char *p = nb_pfs_nodes[t].data;
    const unsigned char *end = p + nb_pfs_nodes[t].size;
    int ret = -1;

    while (p < end)
    {
        const unsigned char *eol = p;
        const unsigned char *k, *eq, *ke, *v, *ve;
        int i;

        while (eol < end && *eol != '\n')
            eol++;

        k = p;
        while (k < eol && (*k == ' ' || *k == '\t'))
            k++;
        if (k >= eol || *k == '#')          /* blank or a comment line   */
        {
            p = (eol < end) ? eol + 1 : end;
            continue;
        }

        /* the first line with words in it is the one that decides */
        eq = k;
        while (eq < eol && *eq != '=')
            eq++;
        if (eq > k && eq < eol)
        {
            ke = eq;
            while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t'))
                ke--;
            v = eq + 1;
            while (v < eol && (*v == ' ' || *v == '\t'))
                v++;
            ve = eol;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' ||
                              ve[-1] == '\r'))
                ve--;
            if (tok_is(k, ke, "program"))
                for (i = 0; i < N_PROGRAMS; i++)
                    if (tok_is(v, ve, prog_name[i]))
                        ret = i;
        }
        break;
    }
    return ret;
}
