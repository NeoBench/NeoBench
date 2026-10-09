#ifndef NB_PROGS_H
#define NB_PROGS_H

/*
 * The program table: what the desktop can run, and the one function
 * that decides whether a file in the store is one of them.
 *
 * This was three statics inside main.c -- N_PROGRAMS, the prog_name[]
 * list, and program_slot_of() with its tok_is() -- which meant the
 * answer to "is this file a program?" could only be asked from inside
 * the desktop's own translation unit.  Phase 2 of the Rust migration
 * holds that parser against its Rust twin, on the host
 * (tools/tests/test_store.c) and at boot (rs_store() in
 * kernel/init/kernel_main.c), and both need it linkable; the three
 * moved out together rather than being copied, so there is still one
 * list and one parser to keep in step with the store's entries.
 *
 * Nothing about them changed on the way out: prog_name[] is still the
 * names a `program =` line may carry, in slot order, and
 * program_slot_of() still reads only the first line of a file that is
 * neither blank nor a comment.
 */

#define N_PROGRAMS  8            /* programs the desktop can run         */

/*
 * The names a `program =` line may carry, one per slot, in the order
 * the slots are numbered.  The store's entries are written against
 * this list and NeoShell's `progs` and `run` ask it the same question,
 * so a program added here cannot be missing from a drawer or from the
 * command line.
 */
extern const char *const prog_name[N_PROGRAMS];

/* One word of a file, compared without regard to case: the bytes
 * [s, e) against the NUL-terminated word w. */
int tok_is(const unsigned char *s, const unsigned char *e,
           const char *w);

/*
 * Is store node t a program rather than a document?  Answers the start
 * menu's program slot, or -1 for a document.
 */
int program_slot_of(unsigned t);

#endif /* NB_PROGS_H */
