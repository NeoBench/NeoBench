#ifndef NB_INFLATE_H
#define NB_INFLATE_H

/*
 * Raw DEFLATE (RFC 1951), the compression the page content streams of a
 * PDF arrive in.
 *
 * This exists so that NeoText can open a document and read the text out
 * of it, and for nothing else: no zlib header, no gzip wrapper, no
 * dictionary, no streaming interface.  A PDF carries the raw stream
 * between `stream' and `endstream', which is exactly what the one caller
 * has in hand.
 *
 * The decoder is freestanding -- no allocation, no recursion, no libc --
 * because it is linked into the ROM alongside everything else NeoBench
 * boots with, and it carries no hardware knowledge at all, so
 * tools/tests/test_pdf.c compiles it straight into a host binary and
 * drives it from data zlib produced.
 */

/*
 * Decompress slen bytes at src into dst.  Returns the number of bytes
 * written, or -1 when the stream is malformed, truncated, or does not
 * fit in dcap.  Either answer is final: there is no partial success to
 * retry, and a caller that gets -1 back should show the file as the
 * bytes it is rather than as text.
 */
int nb_inflate(const unsigned char *src, unsigned slen,
               unsigned char *dst, unsigned dcap);

#endif /* NB_INFLATE_H */
