/*
 * test_pdf.c -- the document reader, on the host.
 *
 * Two things are under test.  inflate.c is a pure decoder, so the honest
 * check is against data zlib produced: four streams covering every block
 * type the format has, then the ways a stream can be wrong.  pdf.c is
 * then driven with a document assembled here, one object per behaviour --
 * a compressed page, an image that must not be read, a page with no
 * filter, a stream that is not a page at all -- so that a change which
 * reads the wrong dictionary, or puts a line break where there is not
 * one, fails here rather than on the machine.
 */
#include <stdio.h>
#include <string.h>

#include "../../boot/rom/inflate.h"
#include "../../boot/rom/pdf.h"

static int fails;

static void check(int cond, const char *what)
{
    if (!cond)
    {
        printf("FAIL: %s\n", what);
        fails++;
    }
}

/* ------------------------------------------------------------------ *
 * inflate
 * ------------------------------------------------------------------ */

/* zlib, raw DEFLATE, level 9. */
static const unsigned char blob_fixed[10] = {
    0xf3, 0x4b, 0xcd, 0x77, 0x4a, 0xcd, 0x4b, 0xce, 0x00, 0x00,
};

static const unsigned char blob_dyn[22] = {
    0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xf0, 0x4b, 0xcd, 0x77,
    0x4a, 0xcd, 0x4b, 0xce, 0xd0, 0xe3, 0xf2, 0x18, 0xac, 0x02, 0x00,
};

static const unsigned char blob_long[62] = {
    0x0b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56, 0x48,
    0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a,
    0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01,
    0x4a, 0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0xeb, 0x29,
    0x84, 0x8c, 0x2a, 0x1e, 0x55, 0x3c, 0xaa, 0x78, 0x54, 0xf1, 0xa8,
    0xe2, 0x51, 0xc5, 0xc3, 0x4b, 0x31, 0x00,
};

static const unsigned char blob_zero[9] = {
    0x63, 0x60, 0x18, 0x05, 0xa3, 0x60, 0x68, 0x02, 0x00,
};

static const unsigned char blob_pdf[183] = {
    0x4d, 0x8f, 0x4d, 0x0f, 0x01, 0x41, 0x0c, 0x86, 0xef, 0x7e, 0xc5,
    0x7b, 0x1c, 0x42, 0xec, 0x88, 0xe0, 0x6a, 0x85, 0x83, 0x83, 0x88,
    0xf4, 0x26, 0x0e, 0x93, 0x9d, 0xda, 0x1d, 0xec, 0x0c, 0xbb, 0x23,
    0xfc, 0x7c, 0xb5, 0x3e, 0xd3, 0x1e, 0xfa, 0xf1, 0xf4, 0x6d, 0x9b,
    0x12, 0xfa, 0x0b, 0x0d, 0x3d, 0x01, 0xed, 0x31, 0x1e, 0x88, 0x27,
    0x20, 0x0b, 0xb5, 0xe2, 0x90, 0xb2, 0xcf, 0x0a, 0x5c, 0xae, 0x2e,
    0x3b, 0xa2, 0x8e, 0xa6, 0x8a, 0x6d, 0xd0, 0x01, 0xd4, 0x81, 0xda,
    0xb0, 0xb1, 0xce, 0xe7, 0xb0, 0x21, 0xbb, 0x96, 0xec, 0x63, 0x0d,
    0xe7, 0x21, 0x13, 0xc4, 0xf7, 0x1f, 0x34, 0x2b, 0xcc, 0x39, 0x72,
    0x05, 0xfd, 0xa9, 0xa4, 0x84, 0x2d, 0x54, 0xf0, 0xdc, 0x46, 0x4f,
    0xcb, 0x1a, 0x15, 0x6f, 0xe1, 0x1b, 0x16, 0x15, 0x4b, 0x7d, 0x07,
    0x5a, 0x62, 0x4e, 0x2d, 0x41, 0xe5, 0x96, 0xd1, 0xe8, 0x75, 0xcb,
    0x14, 0x35, 0x67, 0xc1, 0x5b, 0x9c, 0x9c, 0xe7, 0xae, 0x24, 0x11,
    0x37, 0x17, 0x0b, 0xe9, 0x35, 0xd2, 0x2f, 0x5e, 0x23, 0x11, 0xd3,
    0xcd, 0xdc, 0xf3, 0x87, 0x12, 0x6a, 0x1d, 0x6a, 0x17, 0x9d, 0x2c,
    0xb4, 0x6f, 0xbe, 0x6c, 0xf8, 0x7f, 0x32, 0x19, 0x36, 0xa4, 0x11,
    0x71, 0x93, 0x1b, 0xe7, 0x3f, 0x82, 0x0f,
};

/* A stored block: the format's one uncompressed form, built here so
 * that path is covered whether or not zlib happens to emit one. */
static int stored_block(const char *text, unsigned char *out)
{
    unsigned n = (unsigned)strlen(text);
    unsigned i;

    out[0] = 0x01;                      /* final, type 0: stored        */
    out[1] = (unsigned char)(n & 0xff);
    out[2] = (unsigned char)((n >> 8) & 0xff);
    out[3] = (unsigned char)((~n) & 0xff);
    out[4] = (unsigned char)((~n >> 8) & 0xff);
    for (i = 0; i < n; i++)
        out[5 + i] = (unsigned char)text[i];
    return 5 + (int)n;
}

static void test_inflate(void)
{
    unsigned char out[4096];
    unsigned char stored[256];
    int n;

    n = nb_inflate(blob_fixed, sizeof(blob_fixed), out, sizeof(out));
    check(n == 8 && memcmp(out, "NeoBench", 8) == 0,
          "fixed Huffman block decodes");

    n = nb_inflate(blob_dyn, sizeof(blob_dyn), out, sizeof(out));
    check(n == 170, "dynamic block length");
    check(n == 170 && memcmp(out, "Hello, NeoBench.\n", 17) == 0,
          "dynamic block start");
    if (n == 170)
        check(memcmp(out, out + 17, 153) == 0, "back-references repeat");

    n = nb_inflate(blob_long, sizeof(blob_long), out, sizeof(out));
    check(n == 1800, "long text length");
    check(n == 1800 &&
          memcmp(out, "The quick brown fox jumps over the lazy dog. ", 45) == 0,
          "long text start");

    n = nb_inflate(blob_zero, sizeof(blob_zero), out, sizeof(out));
    check(n == 700, "run of zeroes length");
    if (n == 700)
    {
        int i, all = 1;

        for (i = 0; i < 700; i++)
            if (out[i])
                all = 0;
        check(all, "run of zeroes is zeroes");
    }

    n = stored_block("A stored block, copied through untouched.", stored);
    {
        int got = nb_inflate(stored, (unsigned)n, out, sizeof(out));

        check(got == n - 5 &&
              memcmp(out, "A stored block, copied through untouched.",
                     (size_t)(n - 5)) == 0,
              "stored block decodes");
    }

    /* the ways it is allowed to say no */
    check(nb_inflate(blob_dyn, sizeof(blob_dyn) - 3, out, sizeof(out)) < 0,
          "truncated stream fails");
    check(nb_inflate(stored, 3, out, sizeof(out)) < 0,
          "stored block without its data fails");
    {
        static const unsigned char bad_type[1] = { 0x07 };  /* type 3 */

        check(nb_inflate(bad_type, 1, out, sizeof(out)) < 0,
              "reserved block type fails");
    }
    {
        static const unsigned char bad_len[6] = {
            0x01, 0x05, 0x00, 0x00, 0x00, 0x41     /* NLEN does not match */
        };

        check(nb_inflate(bad_len, 6, out, sizeof(out)) < 0,
              "stored block with a bad complement fails");
    }
    check(nb_inflate(blob_fixed, sizeof(blob_fixed), out, 4) < 0,
          "output that does not fit fails");
    check(nb_inflate(blob_fixed, 0, out, sizeof(out)) < 0,
          "no input fails");
}

/* ------------------------------------------------------------------ *
 * pdf
 * ------------------------------------------------------------------ */

/* The document being built: put_s/put_b append here, and stream_obj
 * writes one object through them.  Switching `put' builds a second
 * document without disturbing the first. */
static unsigned char doc[16384];
static unsigned doc_n;
static unsigned char *put;
static unsigned put_n;

static void put_s(const char *s)
{
    while (*s)
        put[put_n++] = (unsigned char)*s++;
}

static void put_b(const unsigned char *b, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++)
        put[put_n++] = b[i];
}

static void stream_obj(int num, const char *dict,
                       const unsigned char *body, unsigned blen)
{
    char head[160];

    sprintf(head, "%d 0 obj\n<< %s/Length %u >>\nstream\n", num, dict, blen);
    put_s(head);
    put_b(body, blen);
    put_s("\nendstream\nendobj\n");
}

/*
 * Four objects, each one behaviour: a compressed page, an image (whose
 * dictionary must stop the reader even though its bytes would parse),
 * a page with no filter at all, and a stream that decompresses to
 * nothing like text.
 */
static void build_doc(void)
{
    static const unsigned char plain[] =
        "BT 72 700 Td (Plain content stream, no filter) Tj ET\n";
    static const unsigned char not_a_page[] = {
        0x00, 0x01, 0x02, 0xff, 0xfe, 0x00, 0x1f, 0x80, 0x7f, 0x00
    };
    static const unsigned char picture[] =
        "BT (this image must not be read) Tj ET";

    put = doc;
    put_n = 0;
    put_s("%PDF-1.4\n");
    stream_obj(1, "/Filter /FlateDecode ", blob_pdf, sizeof(blob_pdf));
    stream_obj(2, "/Subtype /Image /Width 4 /Height 4 ",
               picture, (unsigned)strlen((const char *)picture));
    stream_obj(3, "", plain, (unsigned)sizeof(plain) - 1);
    stream_obj(4, "/Filter /FlateDecode ", not_a_page, sizeof(not_a_page));
    put_s("trailer\n<< /Root 1 0 R >>\n%%EOF\n");
    doc_n = put_n;
}

static void test_pdf(void)
{
    static const char expect[] =
        "NeoBench quick start\n"
        "Reading documents in NeoText\n"
        "Chapter 1\n"
        "one two three\n"
        "A second line, set with Td\n"
        "Positioned with Tm\n"
        "and again\n"
        "Plain content stream, no filter\n";

    unsigned char scratch[4096];
    char out[4096];
    int n;

    build_doc();

    n = nb_pdf_text(doc, doc_n, out, sizeof(out), scratch, sizeof(scratch));
    check(n == (int)strlen(expect), "extracted length");
    if (n > 0 && n == (int)strlen(expect) && strcmp(out, expect) == 0)
        check(1, "extracted text");
    else
    {
        printf("FAIL: extracted text\n  got: [%s]\n", out);
        fails++;
    }

    /* not a PDF at all */
    check(nb_pdf_text((const unsigned char *)"not a pdf", 9,
                      out, sizeof(out), scratch, sizeof(scratch)) == 0,
          "a file that is not a PDF reads as nothing");
    check(out[0] == '\0', "a non-PDF leaves out empty");

    /* a PDF whose only stream is an image reads as nothing */
    {
        static unsigned char img[4096];

        put = img;
        put_n = 0;
        put_s("%PDF-1.4\n");
        stream_obj(1, "/Subtype /Image /Filter /FlateDecode ",
                   blob_pdf, sizeof(blob_pdf));
        put_s("trailer\n%%EOF\n");
        n = nb_pdf_text(img, put_n, out, sizeof(out), scratch, sizeof(scratch));
        check(n == 0, "an image-only PDF reads as nothing");
        put = doc;                      /* the main document, untouched */
    }

    /* the output buffer is respected */
    {
        char small[16];

        memset(small, 'X', sizeof(small));
        n = nb_pdf_text(doc, doc_n, small, sizeof(small),
                        scratch, sizeof(scratch));
        check(n > 0 && n < (int)sizeof(small),
              "small buffer stays bounded");
        if (n > 0)
        {
            check(strlen(small) == (size_t)n, "small buffer is terminated");
            check(memcmp(small, expect, (size_t)n) == 0,
                  "small buffer holds the start of the text");
        }
    }
}

int main(void)
{
    test_inflate();
    test_pdf();

    if (fails)
    {
        printf("test_pdf: %d check(s) failed\n", fails);
        return 1;
    }
    printf("test_pdf: all checks pass\n");
    return 0;
}
