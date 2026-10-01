/*
 * pdf.c -- the text of a PDF, without the rest of it.
 *
 * A PDF is a bag of objects.  Most are compressed with DEFLATE, and the
 * ones that matter here -- the pages -- hold a content stream: a small
 * PostScript program made of positioning operators and text-showing
 * operators.  Reading the document therefore takes three steps, and this
 * file is those three:
 *
 *   1. find each `stream' ... `endstream', and skip the ones that are not
 *      page text: an image, an inline image, a font program, an object
 *      stream.  What is left is either DEFLATE compressed (inflated with
 *      inflate.c into the caller's scratch buffer) or plain;
 *   2. check the bytes really do look like a content stream before
 *      parsing them, because a compressed image will decompress happily
 *      and mean nothing;
 *   3. walk that program as tokens and keep what it shows: strings,
 *      wherever they come from, flushed by the operator that shows them,
 *      and a line break wherever the program moves to a new line.
 *
 * What comes out is the text in reading order, not the page.  There is no
 * font table here and no glyph encoding: a document set through a subset
 * or CID font shows its character codes rather than its letters, which is
 * why this returns 0 rather than a page of noise when a stream does not
 * look like text at all.  Layout -- columns, tables, the space a picture
 * left -- is not recoverable from text-showing operators and is not
 * pretended to be.
 *
 * No floating point: PDF numbers are compared as written.  A zero is a
 * zero whatever it is spelled as, and a kern of -120.5 is a word space
 * because it is below -100, not because its value was computed.
 */
#include "pdf.h"
#include "inflate.h"

/* ------------------------------------------------------------------ *
 * Bytes, words, and where they are
 * ------------------------------------------------------------------ */

static int pdf_ws(unsigned char c)
{
    return c == ' '  || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f'  || c == '\0';
}

/*
 * A delimiter: nothing that can be part of a name, a number or an
 * operator may follow one, which is what makes the flat token walk below
 * unambiguous.
 */
static int pdf_delim(unsigned char c)
{
    return pdf_ws(c) || c == '(' || c == ')' || c == '<' || c == '>' ||
           c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
           c == '%';
}

/* Offset of `what' in s, or -1 when it is not there. */
static long find_at(const unsigned char *s, unsigned n,
                    const char *what, unsigned wlen)
{
    unsigned i;

    if (wlen > n)
        return -1;
    for (i = 0; i + wlen <= n; i++)
    {
        unsigned k = 0;

        while (k < wlen && s[i + k] == (unsigned char)what[k])
            k++;
        if (k == wlen)
            return (long)i;
    }
    return -1;
}

static int mem_has(const unsigned char *s, unsigned n, const char *what)
{
    unsigned w = 0;

    while (what[w])
        w++;
    return find_at(s, n, what, w) >= 0 ? 1 : 0;
}

/* The last occurrence of `what' between two offsets, or -1. */
static long find_last(const unsigned char *s, unsigned from, unsigned to,
                      const char *what, unsigned wlen)
{
    long best = -1;
    unsigned i;

    for (i = from; i + wlen <= to; i++)
    {
        unsigned k = 0;

        while (k < wlen && s[i + k] == (unsigned char)what[k])
            k++;
        if (k == wlen)
            best = (long)i;
    }
    return best;
}

/* Into what the desktop's face can draw: the printable range and a full
 * stop for everything else, with a real line break kept as one. */
static unsigned char typeable(unsigned v)
{
    if (v == (unsigned)'\n')
        return '\n';
    if (v >= 32u && v < 127u)
        return (unsigned char)v;
    return '.';
}

/* ------------------------------------------------------------------ *
 * The reader's state
 * ------------------------------------------------------------------ */

struct ctx
{
    const unsigned char *p, *end;   /* the stream being read            */
    char     *out;
    unsigned  cap;
    unsigned  o;                    /* bytes written so far             */
    char      pend[256];            /* a string waiting to be shown     */
    unsigned  npend;
    int       dirty;                /* the line being built has type    */
    int       prev;                 /* what ended the last line         */
    unsigned  base;                 /* where this page's text began     */
};

/* What happened last: nothing yet on this page, a move to a new line, or
 * the end of a line.  Two moves in a row are a blank line; a move after
 * the end of a line is just the next line's first position. */
#define EV_NONE 0
#define EV_MOVE 1
#define EV_END  2

static void put_ch(struct ctx *c, unsigned char ch)
{
    if (c->o + 1u >= c->cap)
        return;
    c->out[c->o++] = (char)ch;
    c->out[c->o] = '\0';
    c->dirty = (ch != '\n');
}

/*
 * End the line, if there is one to end -- and say whether this was a
 * move down the page, because that is what tells the three cases apart:
 *
 *   - type on the line: end it;
 *   - a move with nothing shown since the previous one: a blank line,
 *     which is how a document separates paragraphs when it is not
 *     showing a line at a time;
 *   - a move at the start of a line just ended, or the move that
 *     positions the first line of a page: nothing at all, because every
 *     document opens with one of those.
 */
static void end_line(struct ctx *c, int move)
{
    if (c->dirty)
    {
        put_ch(c, '\n');
        c->prev = move ? EV_MOVE : EV_END;
    }
    else if (move && c->prev == EV_MOVE && c->o > c->base)
        put_ch(c, '\n');                /* the blank line itself        */
    else if (move)
        c->prev = EV_MOVE;
}

static void pend_ch(struct ctx *c, unsigned char ch)
{
    if (c->npend + 1u >= sizeof(c->pend))
        return;
    c->pend[c->npend++] = (char)ch;
}

static void pend_flush(struct ctx *c)
{
    unsigned i;

    for (i = 0; i < c->npend; i++)
        put_ch(c, (unsigned char)c->pend[i]);
    c->npend = 0;
}

static int tok_is(const char *tok, const char *want)
{
    while (*want)
    {
        if (*tok != *want)
            return 0;
        tok++;
        want++;
    }
    return *tok == '\0';
}

/* "0", "-0", "0.000" ... : a move of nothing is no move. */
static int num_is_zero(const char *t)
{
    int i = 0;

    if (t[i] == '-' || t[i] == '+')
        i++;
    if (t[i] != '0')
        return 0;
    for (i++; t[i]; i++)
        if (t[i] != '0' && t[i] != '.')
            return 0;
    return 1;
}

/* The integer part of a number, with its sign.  Enough to tell a kern of
 * -120 from one of -0.4, and no floating point anywhere. */
static int num_int(const char *t)
{
    int i = 0, sign = 1, v = 0;

    if (t[i] == '-')
    {
        sign = -1;
        i++;
    }
    else if (t[i] == '+')
        i++;

    while (t[i] >= '0' && t[i] <= '9')
    {
        if (v < 100000)
            v = v * 10 + (t[i] - '0');
        i++;
    }
    return sign * v;
}

/* ------------------------------------------------------------------ *
 * The three ways a string reaches the page
 * ------------------------------------------------------------------ */

/*
 * A literal string, '(' ... ')'.  Parentheses nest and are counted; the
 * escapes are the format's own -- the standard control ones, an escaped
 * delimiter, and one to three octal digits for an arbitrary byte.  Bytes
 * outside the printable range arrive as a full stop, because the face the
 * desktop draws with has that range and nothing else.
 */
static unsigned read_str(struct ctx *c, unsigned i)
{
    const unsigned char *s = c->p;
    unsigned n = (unsigned)(c->end - c->p);
    int depth = 1;

    while (i < n)
    {
        unsigned char ch = s[i++];

        if (ch == '\\')
        {
            unsigned char e;
            unsigned v, k;

            if (i >= n)
                break;
            e = s[i++];
            switch (e)
            {
            case 'n': pend_ch(c, '\n'); continue;
            case 'r': pend_ch(c, '\n'); continue;
            case 't': pend_ch(c, ' ');  continue;
            case 'b': case 'f':         continue;   /* not a line break  */
            case '(': case ')': case '\\':
                pend_ch(c, e); continue;
            case '\r':
                if (i < n && s[i] == '\n') i++;
                continue;
            case '\n':
                continue;
            default:
                break;
            }
            if (e < '0' || e > '7')
                continue;
            v = e - '0';
            for (k = 1; k < 3 && i < n && s[i] >= '0' && s[i] <= '7'; k++)
            {
                v = v * 8u + (unsigned)(s[i] - '0');
                i++;
            }
            pend_ch(c, typeable(v));
            continue;
        }

        if (ch == '(')
        {
            depth++;
            pend_ch(c, '(');
            continue;
        }
        if (ch == ')')
        {
            if (--depth == 0)
                return i;
            pend_ch(c, ')');
            continue;
        }
        if (ch == '\r')
        {
            if (i < n && s[i] == '\n')
                i++;
            pend_ch(c, '\n');
            continue;
        }
        pend_ch(c, typeable(ch));
    }
    return i;
}

/* A hex string, '<48656C6C6F>'.  One odd digit at the end is padded. */
static unsigned read_hex(struct ctx *c, unsigned i)
{
    const unsigned char *s = c->p;
    unsigned n = (unsigned)(c->end - c->p);
    int hi = -1;

    while (i < n)
    {
        unsigned char ch = s[i++];
        int d;

        if (ch == '>')
            break;
        if      (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else                             continue;

        if (hi < 0)
            hi = d;
        else
        {
            pend_ch(c, typeable((unsigned)((hi << 4) | d)));
            hi = -1;
        }
    }
    if (hi >= 0)
        pend_ch(c, typeable((unsigned)(hi << 4)));
    return i;
}

/*
 * One token: a run of bytes none of which is a delimiter.  Operators,
 * names and numbers all come out the same way -- what they are is decided
 * by the byte they started with, which is the whole of the grammar this
 * parser needs.
 */
static unsigned read_tok(struct ctx *c, unsigned i, char *dst, unsigned cap)
{
    const unsigned char *s = c->p;
    unsigned n = (unsigned)(c->end - c->p);
    unsigned k = 0;

    while (i < n && !pdf_delim(s[i]))
    {
        if (k + 1u < cap)
            dst[k++] = (char)s[i];
        i++;
    }
    if (cap)
        dst[k] = '\0';
    return i;
}

/* An inline image runs from `BI' to a bare `EI'; its bytes are data. */
static unsigned skip_image(struct ctx *c, unsigned i)
{
    unsigned n = (unsigned)(c->end - c->p);

    while (i + 1u < n)
    {
        if (c->p[i] == 'E' && c->p[i + 1] == 'I' &&
            (i == 0 || pdf_ws(c->p[i - 1])) &&
            (i + 2u >= n || pdf_ws(c->p[i + 2])))
            return i + 2u;
        i++;
    }
    return n;
}

/* ------------------------------------------------------------------ *
 * The content stream
 * ------------------------------------------------------------------ */

static void content(struct ctx *c)
{
    unsigned n = (unsigned)(c->end - c->p);
    unsigned i = 0;
    char nums[6][24];               /* the numbers immediately preceding */
    char lastf[24];                 /* the y of the last Tm              */
    int nnums = 0, depth = 0, have_lastf = 0;

    c->npend = 0;
    c->dirty = 0;
    c->prev  = EV_NONE;
    c->base  = c->o;
    lastf[0] = '\0';

    while (i < n)
    {
        unsigned char ch = c->p[i];
        char tok[40];

        if (pdf_ws(ch))
        {
            i++;
            continue;
        }

        if (ch == '%')                       /* a comment, to end of line */
        {
            while (i < n && c->p[i] != '\n' && c->p[i] != '\r')
                i++;
            continue;
        }

        if (ch == '(')                       /* a literal string          */
        {
            i = read_str(c, i + 1);
            continue;
        }

        if (ch == '<')
        {
            if (i + 1u < n && c->p[i + 1] == '<')
            {
                i += 2;                      /* a dictionary: not text    */
                continue;
            }
            i = read_hex(c, i + 1);
            continue;
        }

        if (ch == '[')                       /* an array: TJ's operand    */
        {
            depth++;
            c->npend = 0;
            i++;
            continue;
        }
        if (ch == ']')
        {
            if (depth)
                depth--;
            i++;
            continue;
        }

        if (ch == '/')                       /* a name: a font, a colour  */
        {
            i++;
            while (i < n && !pdf_delim(c->p[i]))
                i++;
            continue;
        }

        if (ch == '-' || ch == '+' || ch == '.' ||
            (ch >= '0' && ch <= '9'))
        {
            unsigned k;

            i = read_tok(c, i, tok, sizeof(tok));
            if (nnums < 6)
            {
                for (k = 0; tok[k] && k + 1u < sizeof(nums[0]); k++)
                    nums[nnums][k] = tok[k];
                nums[nnums][k] = '\0';
                nnums++;
            }
            /*
             * Inside a text-showing array a large negative number pushes
             * the next string along -- that is how a PDF spaces words.
             */
            if (depth && num_int(tok) <= -100)
                pend_ch(c, ' ');
            continue;
        }

        i = read_tok(c, i, tok, sizeof(tok));
        if (tok[0] == '\0')                  /* a delimiter with no name  */
        {
            i++;
            continue;
        }

        if (tok[0] == '\'' && tok[1] == '\0')
        {
            end_line(c, 1);                 /* ' : next line, then show  */
            pend_flush(c);
        }
        else if (tok[0] == '"' && tok[1] == '"' && tok[2] == '\0')
        {
            end_line(c, 1);                 /* " : the same, with style  */
            pend_flush(c);
        }
        else if (tok_is(tok, "Tj") || tok_is(tok, "TJ"))
            pend_flush(c);                   /* show what was accumulated */
        else if (tok_is(tok, "T*"))
            end_line(c, 1);                  /* the next line of a block  */
        else if (tok_is(tok, "Td") || tok_is(tok, "TD"))
        {
            /* a move down the page ends the line it moved from; a move
             * along it, whose y is nothing, does not */
            if (nnums < 2 || !num_is_zero(nums[1]))
                end_line(c, 1);
        }
        else if (tok_is(tok, "Tm"))
        {
            /* the same test against the last line's y, which is what a
             * generator that positions every line itself leaves behind */
            if (nnums >= 6)
            {
                unsigned k;

                if (!have_lastf || !tok_is(nums[5], lastf))
                    end_line(c, 1);
                for (k = 0; nums[5][k] && k + 1u < sizeof(lastf); k++)
                    lastf[k] = nums[5][k];
                lastf[k] = '\0';
                have_lastf = 1;
            }
        }
        else if (tok_is(tok, "BT") || tok_is(tok, "ET"))
        {
            end_line(c, 0);                 /* a text object's own edges */
            c->npend = 0;
        }
        else if (tok_is(tok, "BI"))
        {
            i = skip_image(c, i);
            nnums = 0;
            continue;
        }

        nnums = 0;
    }
}

/* ------------------------------------------------------------------ *
 * Which streams are worth reading
 * ------------------------------------------------------------------ */

/* Things a content stream's dictionary never carries, and a page's never
 * lacks: an image, a font program, a packed object table. */
static int skip_object(const unsigned char *dict, unsigned dlen)
{
    return mem_has(dict, dlen, "/Image")         ||
           mem_has(dict, dlen, "/DCTDecode")     ||
           mem_has(dict, dlen, "/JPXDecode")     ||
           mem_has(dict, dlen, "/CCITTFaxDecode")||
           mem_has(dict, dlen, "/ObjStm")        ||
           mem_has(dict, dlen, "/Type1C")        ||
           mem_has(dict, dlen, "/CIDFontType0C");
}

/*
 * The last check before parsing: does this look like a program that
 * draws type?  A compressed image inflates into bytes that happen to be
 * there, and reading them as operators would put noise on the page.
 */
static int looks_like_text(const unsigned char *s, unsigned n)
{
    return find_at(s, n, "BT",  2) >= 0 ||
           find_at(s, n, "Tj",  2) >= 0 ||
           find_at(s, n, "TJ",  2) >= 0 ||
           find_at(s, n, "Tf",  2) >= 0;
}

/* ------------------------------------------------------------------ *
 * The entry point
 * ------------------------------------------------------------------ */

int nb_pdf_text(const unsigned char *src, unsigned slen,
                char *out, unsigned ocap,
                unsigned char *scratch, unsigned scap)
{
    struct ctx c;
    unsigned i;

    if (!out || ocap == 0)
        return 0;
    out[0] = '\0';
    if (!src || slen < 5)
        return 0;
    if (src[0] != '%' || src[1] != 'P' || src[2] != 'D' ||
        src[3] != 'F' || src[4] != '-')
        return 0;

    c.out  = out;
    c.cap  = ocap;
    c.o    = 0;
    c.p    = src;
    c.end  = src + slen;
    c.npend = 0;
    c.dirty = 0;

    i = 0;
    while (i + 6u <= slen)
    {
        long kw = find_at(src + i, slen - i, "stream", 6);
        unsigned off, data, e, d0, dlen, blen;
        const unsigned char *dict, *body;

        if (kw < 0)
            break;
        off = i + (unsigned)kw;

        /* the keyword is only the keyword when an end of line follows
         * it; the same word inside a string is just a word */
        data = off + 6u;
        if (data < slen && src[data] == '\r') data++;
        if (data < slen && src[data] == '\n') data++;
        if (data == off + 6u)
        {
            i = off + 6u;
            continue;
        }

        {
            long es = find_at(src + data, slen - data, "endstream", 9);

            if (es < 0)
                break;
            e = data + (unsigned)es;
        }

        /*
         * The dictionary that governs this stream is the text between
         * this object's `obj' and its `stream' -- not a fixed window
         * backwards, which would pick up the previous object and its
         * image dictionary and refuse a perfectly plain stream because
         * something earlier in the file was a picture.
         */
        d0   = (off > 512u) ? off - 512u : 0u;
        {
            long ob = find_last(src, d0, off, "obj", 3);

            if (ob >= 0 && (unsigned)ob + 3u < off)
                d0 = (unsigned)ob + 3u;
        }
        dlen = off - d0;
        dict = src + d0;
        body = src + data;
        blen = e - data;

        if (!skip_object(dict, dlen))
        {
            if (mem_has(dict, dlen, "/FlateDecode"))
            {
                int n = (scratch && scap)
                        ? nb_inflate(body, blen, scratch, scap) : -1;

                if (n > 0 &&
                    looks_like_text(scratch, (unsigned)n))
                {
                    c.p   = scratch;
                    c.end = scratch + (unsigned)n;
                    content(&c);
                    end_line(&c, 0);
                }
            }
            else if (!mem_has(dict, dlen, "/Filter") &&
                     looks_like_text(body, blen))
            {
                c.p   = body;
                c.end = body + blen;
                content(&c);
                end_line(&c, 0);
            }
        }

        i = e + 9u;
    }

    return (int)c.o;
}
