/*
 * The store's paths and the program table -- phase 2.
 *
 * pfs_find(), pfs_first_child() and pfs_next_child() are how anything
 * in NeoBench reaches a file: the kernel finds Config/ with them, the
 * browser walks directories with them, NeoShell's `cd` walks them.  The
 * table prog_name[] and the parser program_slot_of() are what the
 * desktop's `progs` and `run` read -- the eight names a store entry may
 * carry, and the first-line-of-the-file rule that decides whether
 * Config/Preferences starts Preferences or opens in the reader.  This
 * file answers all six the way the C answers them and is held against
 * the C over the same bytes: on the host in tools/tests/test_store.c,
 * beside the real pfs.c, pfs_data.c and progs.c, and at boot, where
 * the kernel hands this half the table itself and every battery of
 * answers the C half produced (nb_rs_store_check, called from
 * kernel/init/kernel_main.c).  Like phase 1, the point is not to
 * replace the C but to hold the Rust against it; the wire answers with
 * one mask of families and, on a failure, the `at` word that says
 * where inside them.
 *
 * The quirks are the contract, not accidents to be tidied away:
 *
 *   - pfs_find is exact and case-sensitive, with no normalisation of
 *     any kind: no `.', no `..', no folding, and no joining.  The one
 *     special case is the first byte -- an empty query or one that
 *     begins with `/' answers the root, which means "/Config" is the
 *     root and not Config.  The C's comment next to that branch says
 *     `"" or "/"'; its code says any leading slash, and the code is
 *     what the battery pins.
 *
 *   - a NULL query is a miss, not an error, because pfs_find(NULL)
 *     returns NULL.
 *
 *   - pfs_first_child skips a directory's own index; pfs_next_child
 *     does not, and starts at after + 1 in wrapping arithmetic -- so
 *     after = 0xFFFFFFFF walks from the top exactly as the C's
 *     unsigned wrap does.
 *
 *   - program_slot_of reads only the first line of a file that is
 *     neither blank nor a comment, whether or not that line is the
 *     `program = ` one.  "title = x" on the first line and
 *     "program = clock" on the second is a document; a lone carriage
 *     return decides too, because it is a word-bearing line with no
 *     '=' in it.  Key and value fold case; the value trims spaces,
 *     tabs and carriage returns off both ends.
 *
 *   - tok_is stands for sh_is where `run` matches its argument
 *     against the table: both fold ASCII case off both sides and
 *     compare to exhaustion, and the names are lower case both ways,
 *     so one implementation is the semantics of the two.
 *
 * And one habit the machine asks for, the same one prefs.rs states:
 * 32-bit arithmetic only (lib32.c owns the arithmetic helpers and a
 * wider division would drag compiler_builtins onto the link), no
 * unwinding -- a bounds panic hangs a boot, so no position is ever
 * sent for a byte that might not be there -- and every pointer the
 * check dereferences comes in as a parameter with its length beside
 * it, which is the caller's contract exactly as the four file pointers
 * were phase 1's.
 */

/// The battery families `nb_rs_store_check` reports, one bit each, and
/// 0 when every battery agreed.  `at` then holds the index of the
/// first disagreement in the order the batteries are named below.
///
///   0 paths      the queries against pfs_find, at = query index
///   1 walk       the per-directory children walk, at = position in seq
///   2 names      prog_name against PROG_NAME, at = slot (or the count)
///   3 slots      program_slot_of against slot_of, at = node index
///   4 run        `run`-style matching, at = argument index
///   5 next       pfs_next_child over sampled `after' values, at = pair
///
/// and bit 31 alone means the call itself was unusable.
pub const BAD_CALL: u32 = 1 << 31;
pub const F_PATH: u32 = 1 << 0;
pub const F_WALK: u32 = 1 << 1;
pub const F_NAME: u32 = 1 << 2;
pub const F_SLOT: u32 = 1 << 3;
pub const F_RUN: u32 = 1 << 4;
pub const F_NEXT: u32 = 1 << 5;

/// What pfs.h calls PFS_NONE: no index, no hit.
pub const NONE: u32 = 0xFFFF_FFFF;

/* How far a C string is walked before the walk itself gives up.  Every
 * string that reaches this module is a store path, a table name or a
 * literal the caller owns -- all terminated, all far shorter than this
 * -- and the cap exists so that a contract broken on the other side of
 * the wire costs a wrong answer rather than an unbounded read. */
const PATH_CAP: usize = 256;
const NAME_CAP: usize = 64;

/*
 * struct pfs_node, field for field, in the order pfs.h spells them.
 * The layout asserts below are the other half of the equivalence:
 * gcc holds its side in kernel/init/kernel_main.c for the ROM and in
 * tools/tests/test_store.c for the host, and nb_rs_store_size() hands
 * the host test the number this half computed at run time.
 */
#[repr(C)]
pub struct PfsNode {
    pub name: *const u8,
    pub path: *const u8,
    pub parent: u32,
    pub data: *const u8,
    pub size: u32,
    pub dir: u32,
}

/* Every member is four bytes wide, so the offsets are the same on
 * either side of the ABI argument: m68k-linux-gnu aligns an int at two
 * bytes and the host at four, and neither shows through a table of
 * pointers and unsigneds. */
#[cfg(target_arch = "m68k")]
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(offset_of!(PfsNode, name) == 0);
    assert!(offset_of!(PfsNode, path) == 4);
    assert!(offset_of!(PfsNode, parent) == 8);
    assert!(offset_of!(PfsNode, data) == 12);
    assert!(offset_of!(PfsNode, size) == 16);
    assert!(offset_of!(PfsNode, dir) == 20);
    assert!(size_of::<PfsNode>() == 24);
};

#[cfg(not(target_arch = "m68k"))]
const _: () = {
    use core::mem::{offset_of, size_of};
    assert!(offset_of!(PfsNode, name) == 0);
    assert!(offset_of!(PfsNode, path) == 8);
    assert!(offset_of!(PfsNode, parent) == 16);
    assert!(offset_of!(PfsNode, data) == 24);
    assert!(offset_of!(PfsNode, size) == 32);
    assert!(offset_of!(PfsNode, dir) == 36);
    assert!(size_of::<PfsNode>() == 40);
};

/* ------------------------------------------------------------------ *
 * Bytes, treated the way the C treats them
 * ------------------------------------------------------------------ */

/// One C string, from its start to the NUL, walked no further than
/// `cap` bytes.  NULL and an unterminated run are both None: the
/// caller's contract says neither happens, and this is the stop-word
/// if it ever does.
unsafe fn c_str<'a>(p: *const u8, cap: usize) -> Option<&'a [u8]> {
    if p.is_null() {
        return None;
    }
    let mut i = 0usize;
    while i < cap {
        if *p.add(i) == 0 {
            return Some(core::slice::from_raw_parts(p, i));
        }
        i += 1;
    }
    None
}

/// ASCII case folded, the way tok_is and sh_is both fold it: a
/// difference only outside A-Z survives.
const fn fold(c: u8) -> u8 {
    if c >= b'A' && c <= b'Z' {
        c - b'A' + b'a'
    } else {
        c
    }
}

/// One word of a file, compared without regard to case: tok_is()'s
/// semantics over a slice.  Equal only when both sides are consumed
/// together, so "clock" is not "clock ".
///
/// Three shapes here come out of phase 1's note in hexv().  The walk
/// runs over one bound only -- `w.len()`, with `s`'s length asked
/// inside as a plain break -- because `i < s.len() && i < w.len()`
/// makes a `min` of the two, and a min is a value waiting in a
/// diamond: m68k has no conditional move, so the not-taken arm's
/// materialisation lands between the compare and its branch and MOVE
/// writes the condition codes out from under it (the scanner called
/// that form out in this file's first build).  The body is
/// prefs.rs's name_is() verbatim, bind-free, so its copies have
/// nothing to insert at the loop's head beside the entry test.
/// Always inlined: at a call site the lengths are the caller's
/// already-live slice locals, not fresh argument registers, and a
/// standalone body would only ever be read by the gate -- name_is()
/// has no such body either, and prefs.rs's gates stayed clean.
#[inline(always)]
fn tok_is(s: &[u8], w: &[u8]) -> bool {
    let mut i = 0usize;

    while i < w.len() {
        if i >= s.len() || fold(s[i]) != fold(w[i]) {
            return false;
        }
        i += 1;
    }
    i == s.len()
}

/* ------------------------------------------------------------------ *
 * The store's paths: pfs.c's three, over the caller's table
 * ------------------------------------------------------------------ */

/// `pfs_find` over the table the caller passed in.  The first byte of
/// the query decides the two special cases -- empty or leading slash
/// is the root -- and everything after is exact, case-sensitive and
/// unnormalised.  A NULL query or a query that does not name a node
/// answers NONE, which is the C's NULL.
pub unsafe fn find(nodes: &[PfsNode], query: *const u8) -> u32 {
    let q = match c_str(query, PATH_CAP) {
        Some(s) => s,
        None => return NONE,
    };
    if q.is_empty() || q[0] == b'/' {
        return 0;
    }
    let mut i = 0usize;
    while i < nodes.len() {
        if let Some(p) = c_str(nodes[i].path, PATH_CAP) {
            if p == q {
                return i as u32;
            }
        }
        i += 1;
    }
    NONE
}

/// `pfs_first_child`: the first node whose parent is `dir`, skipping
/// the directory's own index the way the C does.
pub unsafe fn first_child(nodes: &[PfsNode], dir: u32) -> u32 {
    let mut i = 0usize;
    while i < nodes.len() {
        if nodes[i].parent == dir && i as u32 != dir {
            return i as u32;
        }
        i += 1;
    }
    NONE
}

/// `pfs_next_child`: the first node after `after` whose parent is
/// `dir`, with no self-index guard -- and `after + 1` in wrapping
/// arithmetic, so NONE walks the table from the top exactly as the
/// C's unsigned wrap does.
pub unsafe fn next_child(nodes: &[PfsNode], dir: u32, after: u32) -> u32 {
    let mut i = after.wrapping_add(1);
    while i < nodes.len() as u32 {
        if nodes[i as usize].parent == dir {
            return i;
        }
        i = i.wrapping_add(1);
    }
    NONE
}

/* ------------------------------------------------------------------ *
 * The table progs and run read, and the parser behind it
 * ------------------------------------------------------------------ */

/// prog_name[], in slot order -- the names a `program =` line may
/// carry, which the store's entries are written against.
pub const PROG_NAME: [&[u8]; 8] = [
    b"files",
    b"clock",
    b"monitor",
    b"about",
    b"preferences",
    b"neotext",
    b"vlc",
    b"neoshell",
];

/* space and tab -- the bytes program_slot_of's C skips at the head of
 * a word and cuts off its tail, and CR off the tail of a value too. */
fn is_spacetab(c: u8) -> bool {
    c == b' ' || c == b'\t'
}

fn is_spacetabcr(c: u8) -> bool {
    c == b' ' || c == b'\t' || c == b'\r'
}

/* First index of s that is not space or tab: prefs.rs's
 * first_nonspace() over the byte set program_slot_of's C skips -- a
 * plain walk with one bound, answering "the first byte that is not
 * one of these, or the length when they all are", and no length to
 * set down beside the compare that might reject it. */
fn skip_spacetab(s: &[u8]) -> usize {
    let mut i = 0;

    while i < s.len() && is_spacetab(s[i]) {
        i += 1;
    }

    i
}

/* The tail trim, as prefs.rs's rtrim(): the byte named inside an
 * explicit non-empty guard so every compare stands next to the branch
 * that takes it -- a combined `while ve > v && is-space(body[ve-1])'
 * leaves the byte and the shortened position to meet after the test,
 * and the meeting is a value the scheduler may set down anywhere it
 * dominates: on this machine, over the condition codes (the note in
 * rtrim() tells that story).  `keep' says which bytes go -- the key
 * loses spaces and tabs, a value loses a carriage return as well. */
fn rtrim_with(mut s: &[u8], keep: fn(u8) -> bool) -> &[u8] {
    while !s.is_empty() {
        let last = s[s.len() - 1];

        if !keep(last) {
            break;
        }

        s = &s[..s.len() - 1];
    }

    s
}

/// program_slot_of()'s reading of one file body: the first line that
/// is neither blank nor a comment decides, whatever it says, and a
/// document is -1.  Built on prefs.rs's apply() architecture -- the
/// length hoisted once, the line walked as a shrinking sub-slice, the
/// '=' found by position(), both sides trimmed by rtrim_with() --
/// because that is the shape the two gates have already read clean:
/// the index form of the eol walk made the compiler subtract the
/// remaining count, throw it away and put the length back in a MOVE
/// over the codes the branch was about to read (the note in
/// apply()).  Every slice handed to tok_is is inside `body` by
/// construction: each position is walked to a bound before it is
/// read.
pub fn slot_of(body: &[u8]) -> i32 {
    let mut ret: i32 = -1;
    let mut pos = 0usize;
    let len = body.len();

    while pos < len {
        let mut eol = pos;
        let mut rest = &body[pos..];

        /* The line, walked as a shrinking sub-slice rather than an
         * index against len: emptiness is one zero test whose result
         * is the whole of the branch, and the byte test stands on its
         * own so neither condition can be folded back into the other
         * (apply()'s rule). */
        while !rest.is_empty() {
            if rest[0] == b'\n' {
                break;
            }

            rest = &rest[1..];
            eol += 1;
        }

        let line = &body[pos..eol];
        let k = skip_spacetab(line);

        if k < line.len() && line[k] != b'#' {
            /* The first line with words in it is the one that
             * decides: the first '=' after them, and only where it
             * has a word's worth of room on both sides -- a line
             * opening with '=' has none, and gets no second chance,
             * which is the C's `eq > k && eq < eol' with the search
             * stopped at the first '=' either way. */
            if let Some(r) = line[k..].iter().position(|&b| b == b'=') {
                if r > 0 {
                    let eq = k + r;
                    let key = rtrim_with(&line[k..eq], is_spacetab);
                    let tail = &line[eq + 1..];
                    let val = rtrim_with(&tail[skip_spacetab(tail)..], is_spacetabcr);

                    if tok_is(key, b"program") {
                        /* First match wins.  The table's names are
                         * distinct, so the C's own loop -- which
                         * carries the last match out of it -- could
                         * not have run past one either. */
                        let mut i = 0;
                        while i < PROG_NAME.len() {
                            if tok_is(val, PROG_NAME[i]) {
                                ret = i as i32;
                                break;
                            }
                            i += 1;
                        }
                    }
                }
            }
            break;
        }

        /* Blank or a comment: past the newline and on to the next
         * line.  eol never passed len, so stepping it up inside that
         * one guard is the C's `p = (eol < end) ? eol + 1 : end'
         * with no value waiting in a diamond for a branch to read
         * codes a MOVE had rewritten (see hexv() in prefs.rs). */
        pos = eol;
        if pos < len {
            pos += 1;
        }
    }
    ret
}

/// Where `run <arg>` would land: the first table name the argument
/// matches with sh_is()'s folding, or NONE.  tok_is over the same two
/// slices is the same question -- see the header.
pub fn match_index(arg: &[u8]) -> u32 {
    let mut i = 0usize;
    while i < PROG_NAME.len() {
        if tok_is(arg, PROG_NAME[i]) {
            return i as u32;
        }
        i += 1;
    }
    NONE
}

/* ------------------------------------------------------------------ *
 * The door the kernel calls at boot
 * ------------------------------------------------------------------ */

/*
 * Every battery the kernel built, held against this half's own
 * answers, in the order the parameters come:
 *
 *   nodes   the table itself, ncount entries -- the C's
 *           nb_pfs_nodes as this half reads it
 *   queries the paths to try (the table's own paths first), nq of
 *           them, with the C's pfs_find answers in hits
 *   seq     one segment per directory: its index, then every child,
 *           then NONE, nseq entries of it
 *   names   prog_name as the C spells it, nnames of them
 *   slots   the C's program_slot_of for every node, nslots of them
 *           (which must be ncount, or the call cannot stand)
 *   args    the `run` battery, nargs arguments of alens bytes each,
 *           with the C's first-match answers in want
 *   nxt     sampled (directory, after) pairs, nnxt of them, with the
 *           C's pfs_next_child answers in nxt_hit -- may be absent
 *   at      comes back with the index of the first disagreement
 *
 * What comes back is the family mask above -- 0 on every boot any
 * NeoBench has shipped.  The safety of every read is the caller's
 * contract, exactly as the four file pointers were phase 1's: the
 * kernel passes its own static arrays and the ROM's own table, and
 * the host test passes its own.
 */
#[no_mangle]
pub extern "C" fn nb_rs_store_check(
    nodes: *const PfsNode,
    ncount: u32,
    queries: *const *const u8,
    nq: u32,
    hits: *const u32,
    seq: *const u32,
    nseq: u32,
    names: *const *const u8,
    nnames: u32,
    slots: *const u32,
    nslots: u32,
    args: *const *const u8,
    alens: *const u32,
    nargs: u32,
    want: *const u32,
    nxt: *const u32,
    nxt_hit: *const u32,
    nnxt: u32,
    at: *mut u32,
) -> u32 {
    if at.is_null() {
        return BAD_CALL;
    }
    unsafe { *at = 0 };

    if nodes.is_null()
        || queries.is_null()
        || hits.is_null()
        || seq.is_null()
        || names.is_null()
        || slots.is_null()
        || args.is_null()
        || alens.is_null()
        || want.is_null()
    {
        return BAD_CALL;
    }
    if ncount == 0 || nslots != ncount || nq < ncount || nargs == 0 {
        return BAD_CALL;
    }
    if nnxt > 0 && (nxt.is_null() || nxt_hit.is_null()) {
        return BAD_CALL;
    }

    unsafe {
        let tab = core::slice::from_raw_parts(nodes, ncount as usize);
        let qs = core::slice::from_raw_parts(queries, nq as usize);
        let hs = core::slice::from_raw_parts(hits, nq as usize);
        let mut mask: u32 = 0;

        /* Family 0: every query, answered by this half's find().  The
         * first ncount queries are the table's own paths by contract,
         * which is the nq >= ncount guard above; the rest are the
         * caller's edge cases. */
        let mut i = 0usize;
        while i < qs.len() {
            if find(tab, qs[i]) != hs[i] {
                if mask == 0 {
                    *at = i as u32;
                }
                mask |= F_PATH;
            }
            i += 1;
        }

        /* Family 1: the walk, driven from this half's side.  The
         * caller's seq must be the same shape -- header, children,
         * NONE, once for every node in order -- and every child must
         * be what first_child/next_child say it is. */
        let sq = core::slice::from_raw_parts(seq, nseq as usize);
        let mut pos = 0usize;
        /* A `while` with the step at the foot, not `0..ncount`: the
         * range's own `next` runs at the head of the iteration, so the
         * next index is live across the whole body -- helpers and all
         * -- and lands in the stack, with the reload riding between the
         * latch's compare and its branch (the gate's read at 0x02ce).
         * The step here is a statement after the last use, two
         * instructions from the branch it feeds. */
        let mut d = 0usize;
        'walk: while d < ncount as usize {
            if pos >= sq.len() || sq[pos] != d as u32 {
                if mask == 0 {
                    *at = pos as u32;
                }
                mask |= F_WALK;
                break 'walk;
            }
            pos += 1;
            let mut c = first_child(tab, d as u32);
            loop {
                if pos >= sq.len() || sq[pos] != c {
                    if mask == 0 {
                        *at = pos as u32;
                    }
                    mask |= F_WALK;
                    break 'walk;
                }
                pos += 1;
                if c == NONE {
                    break;
                }
                c = next_child(tab, d as u32, c);
            }
            d += 1;
        }
        if pos != sq.len() {
            if mask == 0 {
                *at = pos as u32;
            }
            mask |= F_WALK;
        }

        /* Family 2: the table.  A count that moved is reported at the
         * count (there is no slot index for "there are too many"), and
         * the names themselves are compared byte for byte -- case
         * sensitive, because `progs` prints these strings and two
         * spellings are two lists. */
        if nnames as usize != PROG_NAME.len() {
            if mask == 0 {
                *at = nnames;
            }
            mask |= F_NAME;
        }
        /* One bound, and the cap is a value rather than an exit: a
         * `break` on `s >= PROG_NAME.len()` shares the induction with
         * the loop's own bound, and the two folded into a single limit
         * tested at the latch -- where the compare then ran on the old
         * index with the increment landing between the compare and the
         * branch (the gate's read at 0x0406).  Past the table the name
         * answers `false`, which adds nothing: with nnames past the
         * table the count bit above is already set, mask is no longer
         * zero, and the store below is skipped exactly as the break
         * left it.  The loop runs the count it was handed. */
        let ns = core::slice::from_raw_parts(names, nnames as usize);
        let mut s = 0usize;
        while s < ns.len() {
            let same = if s < PROG_NAME.len() {
                match c_str(ns[s], NAME_CAP) {
                    Some(n) => n == PROG_NAME[s],
                    None => false,
                }
            } else {
                false
            };
            if !same {
                if mask == 0 {
                    *at = s as u32;
                }
                mask |= F_NAME;
            }
            s += 1;
        }

        /* Family 3: the parser, asked the same nodes the C asked. */
        let sl = core::slice::from_raw_parts(slots, nslots as usize);
        let mut t = 0usize;
        while t < sl.len() {
            let n = &tab[t];
            let body = if n.size == 0 || n.data.is_null() {
                &[][..]
            } else {
                core::slice::from_raw_parts(n.data, n.size as usize)
            };
            if slot_of(body) as u32 != sl[t] {
                if mask == 0 {
                    *at = t as u32;
                }
                mask |= F_SLOT;
            }
            t += 1;
        }

        /* Family 4: the `run` battery.  A NULL argument with a length
         * beside it is a call that cannot stand; a NULL with no
         * length is the empty argument, which is a real question. */
        let av = core::slice::from_raw_parts(args, nargs as usize);
        let lv = core::slice::from_raw_parts(alens, nargs as usize);
        let wv = core::slice::from_raw_parts(want, nargs as usize);
        let mut a = 0usize;
        while a < av.len() {
            let len = lv[a] as usize;
            if av[a].is_null() && len > 0 {
                return BAD_CALL;
            }
            let arg = if av[a].is_null() {
                &[][..]
            } else {
                core::slice::from_raw_parts(av[a], len)
            };
            if match_index(arg) != wv[a] {
                if mask == 0 {
                    *at = a as u32;
                }
                mask |= F_RUN;
            }
            a += 1;
        }

        /* Family 5: the sampled next-child answers, in pairs. */
        if nnxt > 0 {
            /* The pair reads are raw, not slice indexing.  The count
             * already bounds the walk -- p below np / 2 puts both
             * pair halves and the answer inside their arrays -- but
             * slice indexing adds its own checks against p * 2, p *
             * 2 + 1 and np, and two bounds over one counter is a min:
             * the backend takes min(x, x - 1) for the trip count, a
             * min is a select, the not-taken arm's materialisation
             * lands between the compare and its branch and MOVE has
             * rewritten the codes the branch reads (prefs.rs's
             * hexv(); the scanner called this site out in both of
             * this build's earlier shapes).  One bound has nothing
             * to choose between.  The reads are the contract the
             * doc-comment already states -- the kernel passes its own
             * static arrays and RS_NX, the host test its own. */
            let np = (nnxt as usize) * 2;
            let mut p = 0usize;
            while p < np / 2 {
                let dir = *nxt.add(p * 2);
                let after = *nxt.add(p * 2 + 1);
                if next_child(tab, dir, after) != *nxt_hit.add(p) {
                    if mask == 0 {
                        *at = p as u32;
                    }
                    mask |= F_NEXT;
                }
                p += 1;
            }
        }

        mask
    }
}

/// The table's size as this half sees it, so the host test can hold
/// rustc's repr(C) against gcc's sizeof at run time as well as both
/// against their own constants at compile time -- nb_rs_prefs_size()
/// over again.
#[no_mangle]
pub extern "C" fn nb_rs_store_size() -> u32 {
    core::mem::size_of::<PfsNode>() as u32
}

/* ------------------------------------------------------------------ *
 * The tables: the C's semantics, pinned value by value
 * ------------------------------------------------------------------ */

#[cfg(test)]
mod tests {
    use super::*;

    /* One node of a table the test owns.  Every pointer comes from a
     * C-string literal, so the bytes are NUL-terminated behind them
     * the way the ROM's table is -- a plain byte-string's pointer
     * would walk off its own end the moment find() looked for the
     * terminator, and an empty one is not a pointer at all. */
    fn mk(
        name: &'static [u8],
        path: &'static [u8],
        parent: u32,
        data: &'static [u8],
        dir: bool,
    ) -> PfsNode {
        PfsNode {
            name: name.as_ptr(),
            path: path.as_ptr(),
            parent,
            data: data.as_ptr(),
            size: data.len() as u32,
            dir: dir as u32,
        }
    }

    /* A table with the shapes the walk has to survive: a root, two
     * directories, children in two of them, and one node whose parent
     * is itself -- impossible in a tree mkpfs would build, which is
     * exactly why it belongs here: first_child skips it and
     * next_child does not, and both answers are the C's. */
    fn table() -> [PfsNode; 7] {
        [
            mk(c"/".to_bytes(), c"".to_bytes(), u32::MAX, b"", true),
            mk(c"Config".to_bytes(), c"Config".to_bytes(), 0, b"", true),
            mk(
                c"boot.cfg".to_bytes(),
                c"Config/boot.cfg".to_bytes(),
                1,
                b"",
                false,
            ),
            mk(c"Self".to_bytes(), c"Self".to_bytes(), 3, b"", true),
            mk(c"Tools".to_bytes(), c"Tools".to_bytes(), 0, b"", true),
            mk(
                c"Clock".to_bytes(),
                c"Tools/Clock".to_bytes(),
                4,
                b"",
                false,
            ),
            mk(
                c"screen.cfg".to_bytes(),
                c"Config/screen.cfg".to_bytes(),
                1,
                b"",
                false,
            ),
        ]
    }

    #[test]
    fn the_table_is_the_c_s() {
        assert_eq!(PROG_NAME.len(), 8);
        assert_eq!(PROG_NAME[0], b"files");
        assert_eq!(PROG_NAME[1], b"clock");
        assert_eq!(PROG_NAME[2], b"monitor");
        assert_eq!(PROG_NAME[3], b"about");
        assert_eq!(PROG_NAME[4], b"preferences");
        assert_eq!(PROG_NAME[5], b"neotext");
        assert_eq!(PROG_NAME[6], b"vlc");
        assert_eq!(PROG_NAME[7], b"neoshell");
    }

    #[test]
    fn tok_is_folds_ascii_and_exhausts_both_sides() {
        assert!(tok_is(b"program", b"program"));
        assert!(tok_is(b"PROGRAM", b"program"));
        assert!(tok_is(b"pRoGrAm", b"PrOgRaM"));
        assert!(!tok_is(b"program", b"program "));
        assert!(!tok_is(b" program", b"program"));
        assert!(!tok_is(b"program", b"progra"));
        assert!(tok_is(b"", b""));
        assert!(!tok_is(b"", b"x"));
        assert!(!tok_is(b"x", b""));
        /* the fold is ASCII and nothing else: A-Z only, so two
         * high bytes that differ stay differing */
        assert!(tok_is(b"\x80", b"\x80"));
        assert!(!tok_is(b"\xc1", b"\xe1"));
    }

    #[test]
    fn slot_of_reads_the_first_meaningful_line_only() {
        assert_eq!(slot_of(b"program = preferences\n"), 4);
        assert_eq!(slot_of(b"# c\nprogram = clock\n"), 1);
        assert_eq!(slot_of(b"\n\nprogram = clock\n"), 1);
        assert_eq!(slot_of(b"  \t \nprogram=clock\n"), 1);
        assert_eq!(slot_of(b"\tprogram=vlc\n"), 6);
        assert_eq!(slot_of(b"program = Neoshell\n"), 7);
        assert_eq!(slot_of(b"PROGRAM = ABOUT\n"), 3);
        assert_eq!(slot_of(b"  program  =  clock  \r\n"), 1);
        assert_eq!(slot_of(b"program = clock"), 1);
        /* two program lines: the first decides, so this is `files' */
        assert_eq!(slot_of(b"program = files\nprogram = vlc\n"), 0);
        /* a word-bearing first line that is not `program = ' closes
         * the file to the question -- this is a document even though
         * line two would have answered */
        assert_eq!(slot_of(b"title = x\nprogram = clock\n"), -1);
        /* a lone carriage return is a word-bearing line with no '=' */
        assert_eq!(slot_of(b"\r\nprogram = clock\n"), -1);
        /* a comment line does not decide, but `#' must be the first
         * word: indented it still counts, since k walks the spaces */
        assert_eq!(slot_of(b"  #program = clock\nprogram = vlc\n"), 6);
        assert_eq!(slot_of(b"#program = clock\n"), -1);
        /* answers the parser refuses rather than guesses */
        assert_eq!(slot_of(b""), -1);
        assert_eq!(slot_of(b"program=\n"), -1);
        assert_eq!(slot_of(b"program = nosuch\n"), -1);
        assert_eq!(slot_of(b"= clock\n"), -1);
        assert_eq!(slot_of(b"programs = clock\n"), -1);
        assert_eq!(slot_of(b"program = clock\nprogram = vlc\n"), 1);
    }

    #[test]
    fn run_lands_where_sh_run_would() {
        assert_eq!(match_index(b"clock"), 1);
        assert_eq!(match_index(b"CLOCK"), 1);
        assert_eq!(match_index(b"Clock"), 1);
        assert_eq!(match_index(b"NeoShell"), 7);
        assert_eq!(match_index(b"files"), 0);
        /* sh_is compares whole words: a prefix is not a match, and
         * neither is the word with anything behind it */
        assert_eq!(match_index(b"c"), NONE);
        assert_eq!(match_index(b"clock "), NONE);
        assert_eq!(match_index(b"clock\t"), NONE);
        assert_eq!(match_index(b"clockwork"), NONE);
        assert_eq!(match_index(b""), NONE);
    }

    #[test]
    fn find_is_exact_case_sensitive_and_root_happy() {
        let t = table();

        unsafe {
            assert_eq!(find(&t, core::ptr::null()), NONE);
            /* the C's first-byte rule: empty or any leading slash is
             * the root -- "/Config" is not Config */
            assert_eq!(find(&t, c"".as_ptr().cast()), 0);
            assert_eq!(find(&t, c"/".as_ptr().cast()), 0);
            assert_eq!(find(&t, c"/Config".as_ptr().cast()), 0);
            assert_eq!(find(&t, c"/Config/boot.cfg".as_ptr().cast()), 0);

            assert_eq!(find(&t, c"Config".as_ptr().cast()), 1);
            assert_eq!(find(&t, c"Config/boot.cfg".as_ptr().cast()), 2);
            assert_eq!(find(&t, c"Tools/Clock".as_ptr().cast()), 5);
            assert_eq!(find(&t, c"Self".as_ptr().cast()), 3);
            /* no folding, no normalisation, no trailing anything */
            assert_eq!(find(&t, c"config".as_ptr().cast()), NONE);
            assert_eq!(find(&t, c"Config/".as_ptr().cast()), NONE);
            assert_eq!(find(&t, c"Config/boot.cfgg".as_ptr().cast()), NONE);
            assert_eq!(find(&t, c"Config/boot.cfg ".as_ptr().cast()), NONE);
            assert_eq!(find(&t, c"Config/./boot.cfg".as_ptr().cast()), NONE);
            assert_eq!(find(&t, c"Nope".as_ptr().cast()), NONE);
        }
    }

    #[test]
    fn the_walk_is_the_c_s_including_its_asymmetry() {
        let t = table();

        unsafe {
            /* first_child skips a directory's own index, so the
             * self-parented node has no children from where it
             * stands */
            assert_eq!(first_child(&t, u32::MAX), 0);
            assert_eq!(first_child(&t, 0), 1);
            assert_eq!(first_child(&t, 1), 2);
            assert_eq!(first_child(&t, 3), NONE);
            assert_eq!(first_child(&t, 4), 5);
            assert_eq!(first_child(&t, 5), NONE);

            assert_eq!(next_child(&t, 1, 2), 6);
            assert_eq!(next_child(&t, 1, 3), 6);
            assert_eq!(next_child(&t, 1, 6), NONE);
            assert_eq!(next_child(&t, 0, 1), 4);
            assert_eq!(next_child(&t, 4, 3), 5);

            /* no self-index guard here: after = 0xFFFFFFFF wraps to
             * the top and finds node 3 as a child of itself */
            assert_eq!(next_child(&t, 3, u32::MAX), 3);
            /* while the same wrap on a real directory starts the
             * walk from node 0 */
            assert_eq!(next_child(&t, 1, u32::MAX), 2);
            assert_eq!(next_child(&t, u32::MAX, u32::MAX), 0);
        }
    }

    #[test]
    fn a_broken_table_costs_a_wrong_answer_not_a_panic() {
        /* A node whose path is NULL is a table no build produces;
         * find() walks C strings with a bound and gives up on this
         * one rather than on the machine. */
        let mut t = table();
        t[1].path = core::ptr::null();

        unsafe {
            assert_eq!(find(&t, c"Config".as_ptr().cast()), NONE);
            /* the broken node costs its own name and nothing else:
             * the rest of the table still answers */
            assert_eq!(find(&t, c"Config/boot.cfg".as_ptr().cast()), 2);
            assert_eq!(find(&t, c"Tools/Clock".as_ptr().cast()), 5);
        }
    }

    #[test]
    fn the_size_is_what_the_c_compilers_say() {
        /* 24 in the ROM (m68k), 40 on a 64-bit host; the host runs
         * here, and tools/tests/test_store.c holds gcc's sizeof
         * against this number at run time as well. */
        let expect = if cfg!(target_arch = "m68k") { 24 } else { 40 };
        assert_eq!(core::mem::size_of::<PfsNode>(), expect);
        assert_eq!(nb_rs_store_size(), expect as u32);
    }
}
