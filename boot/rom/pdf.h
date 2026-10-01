#ifndef NB_PDF_H
#define NB_PDF_H

/*
 * Text out of a PDF.
 *
 * NeoText claims to read every format the store can hold, and a document
 * is the one that is not plain bytes: a PDF is a set of objects, most of
 * them compressed, whose pages are little PostScript programs that say
 * where each piece of type goes.  What this returns is the text those
 * programs show, in the order they show it -- a reader's answer, not a
 * renderer's: no fonts, no ligatures, no glyph encodings, no tables, and
 * nothing of the layout that made the lines land where they did.
 *
 * That is enough to read a document, which is what the reader is for, and
 * it is honest about what it is: a document whose text is set through a
 * subset or CID font shows its character codes rather than its letters,
 * and this function says so by returning 0, which leaves the caller to
 * show the file as the bytes it is.
 */

/*
 * Extract the text of the PDF at src into out, which is NUL terminated on
 * every path that returns more than zero.  scratch/scap is decompression
 * room: a compressed page stream is inflated there, parsed, and then
 * reused for the next one, so it needs to hold the largest stream in the
 * document rather than the whole file.
 *
 * Returns the number of bytes written, or 0 when src is not a PDF or
 * holds no text this reader understands.
 */
int nb_pdf_text(const unsigned char *src, unsigned slen,
                char *out, unsigned ocap,
                unsigned char *scratch, unsigned scap);

#endif /* NB_PDF_H */
