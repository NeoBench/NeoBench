/*
 * inflate.c -- a raw DEFLATE decoder.
 *
 * A DEFLATE stream is a sequence of blocks, each flagged as the last one
 * and each stored in one of three ways: bytes copied through untouched,
 * codes from the fixed Huffman tables that the format defines, or codes
 * from two tables the block itself describes.  All three carry the same
 * payload -- literals and back-references in one table, the distances
 * those references reach back in the other -- so the whole file is the
 * arithmetic of reading a bit at a time plus the two tables.
 *
 * Nothing here allocates and nothing here recurses.  The output window is
 * the caller's buffer, a back-reference reads only inside what has
 * already been written to it, and the two Huffman tables are the only
 * state, on the stack.  Everything a hostile document can ask for is
 * checked before it happens: a code no table defines, a length that runs
 * past the output, a distance that reaches before the start of it, a
 * repeat count that runs past the list of lengths, and input that runs
 * out mid-symbol all end the decode with -1 rather than reading or
 * writing one byte further.
 *
 * The tables and the decode loop follow the shape of zlib's puff(), the
 * smallest complete DEFLATE there is, rewritten for one file with no
 * window history and no allocation: a PDF's streams decompress whole or
 * not at all, and the caller has the space either way.
 */
#include "inflate.h"

#define MAXBITS 15

/* ------------------------------------------------------------------ *
 * Bits: little-endian, read forwards, any number up to sixteen
 * ------------------------------------------------------------------ */

struct bits
{
    const unsigned char *next, *end;
    unsigned long hold;         /* bytes read but not yet consumed     */
    int have;                   /* how many bits of them are in hold   */
    int bad;                    /* input ran out                        */
};

static unsigned getbits(struct bits *b, unsigned n)
{
    unsigned v;

    while (b->have < (int)n)
    {
        if (b->next >= b->end)
        {
            b->bad = 1;
            return 0;
        }
        b->hold += (unsigned long)(*b->next++) << b->have;
        b->have += 8;
    }

    v = (unsigned)(b->hold & ((1UL << n) - 1UL));
    b->hold >>= n;
    b->have -= (int)n;
    return v;
}

static int getbit(struct bits *b)
{
    return (int)getbits(b, 1);
}

/* Throw away the rest of the byte: stored blocks are byte aligned. */
static void align_byte(struct bits *b)
{
    b->hold = 0;
    b->have = 0;
}

/* ------------------------------------------------------------------ *
 * Canonical Huffman
 * ------------------------------------------------------------------ */

struct huff
{
    short count[MAXBITS + 1];   /* codes of each length                 */
    short symbol[288];          /* symbols ordered by length and code   */
};

/*
 * Build the decode tables from one code length per symbol.  Returns 0 for
 * an exact tree, a positive number for one that is short of codes (legal
 * for a distance table with a single code in it, and harmless -- a decode
 * that walks into the gap simply fails), and -1 for an over-subscribed
 * tree, which is a corrupt stream rather than a sparse one.
 */
static int huff_build(struct huff *h, const unsigned char *len, int n)
{
    int i, left, offs[MAXBITS + 1];

    for (i = 0; i <= MAXBITS; i++)
        h->count[i] = 0;
    for (i = 0; i < n; i++)
    {
        if (len[i] > MAXBITS)
            return -1;
        h->count[len[i]]++;
    }

    left = 1;
    for (i = 1; i <= MAXBITS; i++)
    {
        left <<= 1;
        left -= h->count[i];
        if (left < 0)
            return -1;
    }

    offs[1] = 0;
    for (i = 1; i < MAXBITS; i++)
        offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++)
        if (len[i])
            h->symbol[offs[len[i]]++] = (short)i;

    return left;
}

/* Read one code, a bit at a time, and say which symbol it is. */
static int huff_dec(struct bits *b, const struct huff *h)
{
    int len, code = 0, first = 0, index = 0;

    for (len = 1; len <= MAXBITS; len++)
    {
        code |= getbit(b);
        if (b->bad)
            return -1;

        if (code - first < h->count[len])
            return h->symbol[index + (code - first)];

        index += h->count[len];
        first += h->count[len];
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 * The format's two tables
 * ------------------------------------------------------------------ */

/* lengths 257..285 and how many extra bits each carries */
static const unsigned short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned char len_bits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

/* distances 0..29 and their extra bits */
static const unsigned short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97,
    129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
    4097, 6145, 8193, 12289, 16385, 24577
};
static const unsigned char dist_bits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* the order the nineteen code length codes are sent in */
static const unsigned char clc_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static void fixed_tables(struct huff *lit, struct huff *dist)
{
    unsigned char len[288];
    int i;

    for (i = 0; i < 144; i++) len[i] = 8;
    for (; i < 256; i++)      len[i] = 9;
    for (; i < 280; i++)      len[i] = 7;
    for (; i < 288; i++)      len[i] = 8;
    huff_build(lit, len, 288);

    for (i = 0; i < 30; i++)  len[i] = 5;
    huff_build(dist, len, 30);
}

/*
 * The two tables a dynamic block describes: five bits each for how many
 * length and distance codes there are, four for how many of the nineteen
 * code length codes are sent, then that short table, then the real
 * lengths run together and coded through it -- 0..15 literal, 16 a
 * repeat of the last one, 17 a run of zeroes, 18 a longer one.
 */
static int dynamic_tables(struct bits *b, struct huff *lit, struct huff *dist)
{
    unsigned char lens[320];
    struct huff cl;
    unsigned nlit, ndist, ncode, i;
    int sym;

    nlit   = getbits(b, 5) + 257;
    ndist  = getbits(b, 5) + 1;
    ncode  = getbits(b, 4) + 4;
    if (b->bad || nlit > 288 || ndist > 32)
        return -1;

    for (i = 0; i < 19; i++)
        lens[i] = 0;
    for (i = 0; i < ncode; i++)
        lens[clc_order[i]] = (unsigned char)getbits(b, 3);
    if (b->bad || huff_build(&cl, lens, 19) < 0)
        return -1;

    i = 0;
    while (i < nlit + ndist)
    {
        unsigned repeat, val = 0, k;

        sym = huff_dec(b, &cl);
        if (sym < 0)
            return -1;

        if (sym < 16)
        {
            lens[i++] = (unsigned char)sym;
            continue;
        }
        if (sym == 16)
        {
            if (i == 0)
                return -1;
            val = lens[i - 1];
            repeat = getbits(b, 2) + 3;
        }
        else if (sym == 17)
            repeat = getbits(b, 3) + 3;
        else
            repeat = getbits(b, 7) + 11;

        if (b->bad || i + repeat > nlit + ndist)
            return -1;
        for (k = 0; k < repeat; k++)
            lens[i++] = (unsigned char)val;
    }

    if (lens[256] == 0)                 /* no end of block code        */
        return -1;
    if (huff_build(lit, lens, (int)nlit) < 0)
        return -1;
    if (huff_build(dist, lens + nlit, (int)ndist) < 0)
        return -1;
    return 0;
}

/*
 * The codes of one block, into dst.  Returns 0 at the end of block code,
 * -1 at anything else: an undefined symbol, a length that does not fit, a
 * distance that reaches back before anything was written, or input that
 * ran out.
 */
static int block_codes(struct bits *b, const struct huff *lit,
                       const struct huff *dist,
                       unsigned char *dst, unsigned *out, unsigned cap)
{
    for (;;)
    {
        int sym = huff_dec(b, lit);
        unsigned symd;

        if (sym < 0 || b->bad)
            return -1;

        if (sym < 256)
        {
            if (*out >= cap)
                return -1;
            dst[(*out)++] = (unsigned char)sym;
            continue;
        }
        if (sym == 256)
            return 0;

        symd = (unsigned)sym - 257u;
        if (symd >= 29)
            return -1;

        {
            unsigned len  = len_base[symd] + getbits(b, len_bits[symd]);
            unsigned dsym, d, k;

            sym = huff_dec(b, dist);
            if (sym < 0 || b->bad || sym >= 30)
                return -1;

            dsym = (unsigned)sym;
            d = dist_base[dsym] + getbits(b, dist_bits[dsym]);
            if (b->bad || d == 0 || d > *out)
                return -1;
            if (len > cap - *out)
                return -1;

            /* byte by byte: a back-reference may overlap itself, which
             * is the whole point of a run-length copy in this format */
            for (k = 0; k < len; k++)
            {
                dst[*out] = dst[*out - d];
                (*out)++;
            }
        }
    }
}

int nb_inflate(const unsigned char *src, unsigned slen,
               unsigned char *dst, unsigned dcap)
{
    struct bits b;
    unsigned out = 0;
    int last;

    b.next = src;
    b.end  = src + slen;
    b.hold = 0;
    b.have = 0;
    b.bad  = 0;

    do
    {
        int type;

        last = getbit(&b);
        type = (int)getbits(&b, 2);
        if (b.bad)
            return -1;

        if (type == 0)
        {
            unsigned len, nlen, k;

            align_byte(&b);
            if ((unsigned)(b.end - b.next) < 4)
                return -1;

            len  = (unsigned)b.next[0] | ((unsigned)b.next[1] << 8);
            nlen = (unsigned)b.next[2] | ((unsigned)b.next[3] << 8);
            b.next += 4;
            if ((len ^ 0xFFFFu) != nlen)
                return -1;              /* the complement does not match */
            if ((unsigned)(b.end - b.next) < len || len > dcap - out)
                return -1;

            for (k = 0; k < len; k++)
                dst[out++] = *b.next++;
        }
        else if (type == 1)
        {
            struct huff lit, dist;

            fixed_tables(&lit, &dist);
            if (block_codes(&b, &lit, &dist, dst, &out, dcap))
                return -1;
        }
        else if (type == 2)
        {
            struct huff lit, dist;

            if (dynamic_tables(&b, &lit, &dist))
                return -1;
            if (block_codes(&b, &lit, &dist, dst, &out, dcap))
                return -1;
        }
        else
            return -1;                  /* type 3 is not a block        */
    } while (!last);

    return (int)out;
}
