/*
 * NeoBench Kernel - String / Memory Implementation
 * Bare-metal Amiga 68030/040/060
 *
 * Optimised for M68K: uses longword-aligned transfers where possible
 * for maximum bus throughput on 32-bit Fast RAM.
 *
 * Corrections vs v1.0:
 *
 *  1. strlen RETURNS uint32 BUT PROTOTYPE IN string.h SAYS uint32.
 *     This is consistent - but the C standard says strlen returns size_t.
 *     Our size_t is uint32 on 68k, so this is correct.  No change needed,
 *     but verified for clarity.
 *
 *  2. strncpy NUL-PADDING LOOP HAS OFF-BY-ONE.
 *     The original:
 *       while (n && (*d++ = *src++) != '\0') n--;
 *       while (n--) *d++ = '\0';
 *     After the first loop, if the string was shorter than n, d points
 *     one past the NUL that terminated the copy, and n was decremented
 *     once more in the condition.  But n was decremented INSIDE the loop
 *     body too (n--), so when the NUL is hit, n has already been
 *     decremented.  The second loop uses post-decrement n-- which means
 *     it runs while n >= 1 at the start of the test, i.e. it pads n
 *     bytes.  This is actually correct per POSIX - let's verify:
 *     n=5, src="hi" (3 bytes incl NUL): loop copies 'h','i','\0' (n
 *     decrements to 4,3,2 in the body; the NUL copy decrements n to 2
 *     with n-- in the condition).  Wait: the loop is:
 *       while (n && (*d++ = *src++) != '\0') n--;
 *     Each iteration: test n!=0, copy byte, if not NUL: n--.
 *     For 'h': copy, not NUL, n-- -> n=4.
 *     For 'i': copy, not NUL, n-- -> n=3.
 *     For '\0': copy, IS NUL, n stays 3.  Loop exits.
 *     Padding loop: while(3--) -> runs 3 times. Writes 3 NULs.
 *     Total written: 'h','i','\0','\0','\0','\0' = 6 bytes for n=5. WRONG.
 *     Should be exactly 5: 'h','i','\0','\0','\0'.
 *     Fix: decrement n when we copy the NUL too, or use a different form.
 *     Fixed with a simple indexed implementation that is unambiguous.
 *
 *  3. memmove OVERLAP DETECTION WRONG.
 *     The original used:
 *       if (d < s || d >= s + count) return memcpy(dst, src, count);
 *     This is the condition for "no overlap or dst before src" which means
 *     a forward copy is safe.  But it passes to memcpy which may use the
 *     optimised unrolled path - that's fine.  The real issue: what if dst
 *     is AFTER src but there IS overlap?  The condition d >= s + count
 *     would be false, and the fallback backward copy is used correctly.
 *     Actually the logic appears correct.  However the condition is
 *     written with 'd < s' (dst pointer < src pointer) meaning "dst is
 *     entirely before src" which triggers the fast memcpy path.
 *     But if dst overlaps src from below (d < s but d+count > s), a
 *     forward copy is still safe because we're writing below where we
 *     read.  This is correct.
 *     The actual bug: if dst == src, it returns memcpy which is fine.
 *     If dst > src and dst < src + count (overlap from above), we need
 *     backward copy.  Original condition "d >= s + count" catches this:
 *     if d < s+count AND d >= s, we fall through to backward copy. But
 *     the condition "d < s" is checked first and would short-circuit to
 *     memcpy... no wait, the condition is OR: "d < s || d >= s+count".
 *     If d is above src but still overlapping, d >= s (so d < s is false)
 *     and d < s+count (so d >= s+count is false).  Both false -> falls
 *     through to backward copy. Correct.
 *     If d is above src and not overlapping (d >= s+count), the OR is
 *     true -> calls memcpy. Correct (no overlap, forward is fine).
 *     The original logic is correct.  No change needed here.
 *
 *  4. memcpy ALIGNMENT-MISMATCH FALLBACK IS INCOMPLETE.
 *     The original checks if both pointers have the same alignment mod 4.
 *     If they don't, it skips the longword optimisation and falls through
 *     to the final byte loop - but count has already been updated in
 *     the alignment block (count -= align; ... only if alignment path
 *     was taken).  If the pointers are misaligned relative to each other,
 *     the longword block is skipped but count still refers to the full
 *     count, and the byte loop at the bottom handles it correctly.
 *     No bug; but the code structure is confusing - left as-is with
 *     a clarifying comment added.
 *
 *  5. MISSING isalnum AND isxdigit.
 *     neoshell.cpp and neocli.cpp use isalnum implicitly via the
 *     tab_complete and hex parsing paths.  isxdigit is useful for the
 *     mem dump hex parser.  Added both.
 *
 *  6. memset TAIL COMPUTATION IS WRONG IN EDGE CASE.
 *     After the quad and single longword loops:
 *       longs &= 3;
 *       while (longs--) *d32++ = fill;
 *       d = reinterpret_cast<uint8 *>(d32);
 *       uint32 tail = count & 3;
 *     But 'count' here is the original count minus the alignment bytes
 *     and the longword-aligned portion.  After filling longs longwords,
 *     count & 3 gives the tail correctly.  No bug, but it relies on
 *     count not being modified in the unrolled loop, which is true.
 *     Verified correct; added comment.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "string.h"

extern "C" {

/* ======================================================================
 * Memory functions
 * ====================================================================== */

void *memset(void *dst, int val, uint32 count)
{
    uint8 *d = (uint8 *)dst;
    uint8  v = (uint8)val;

    if (count < 16) {
        while (count--) *d++ = v;
        return dst;
    }

    /* Align to 4-byte boundary */
    uint32 align = (4u - ((uint32)d & 3u)) & 3u;
    count -= align;
    while (align--) *d++ = v;

    /* Build 32-bit fill word */
    uint32 fill = v | ((uint32)v << 8) | ((uint32)v << 16) | ((uint32)v << 24);

    /* Longword fill, 4x unrolled */
    uint32 *d32  = (uint32 *)d;
    uint32  longs = count >> 2;
    uint32  quads = longs >> 2;
    while (quads--) {
        d32[0] = fill; d32[1] = fill;
        d32[2] = fill; d32[3] = fill;
        d32 += 4;
    }
    longs &= 3u;
    while (longs--) *d32++ = fill;

    /* Remaining 0-3 bytes (count & 3) */
    d = (uint8 *)d32;
    uint32 tail = count & 3u;
    while (tail--) *d++ = v;

    return dst;
}

void *memcpy(void *dst, const void *src, uint32 count)
{
    uint8       *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;

    if (count < 16) {
        while (count--) *d++ = *s++;
        return dst;
    }

    /*
     * If both pointers have the same alignment mod 4, we can do the
     * aligned longword copy after a short byte prefix.
     * If they differ in alignment, fall through to byte-at-a-time.
     */
    if ((((uint32)d ^ (uint32)s) & 3u) == 0u) {
        uint32 align = (4u - ((uint32)d & 3u)) & 3u;
        count -= align;
        while (align--) *d++ = *s++;

        uint32       *d32 = (uint32 *)d;
        const uint32 *s32 = (const uint32 *)s;

        uint32 longs = count >> 2;
        uint32 quads = longs >> 2;
        while (quads--) {
            d32[0] = s32[0]; d32[1] = s32[1];
            d32[2] = s32[2]; d32[3] = s32[3];
            d32 += 4; s32 += 4;
        }
        longs &= 3u;
        while (longs--) *d32++ = *s32++;

        d = (uint8 *)d32;
        s = (const uint8 *)s32;
        count &= 3u;
    }

    /* Byte tail (also handles full misaligned case since count is unchanged) */
    while (count--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, uint32 count)
{
    uint8       *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;

    if (d == s || count == 0) return dst;

    /*
     * If dst is below src, or dst is at or past the end of the source
     * region, a forward copy cannot overwrite data we haven't read yet.
     */
    if (d < s || d >= s + count) return memcpy(dst, src, count);

    /* Backward copy for dst inside [src, src+count) */
    d += count;
    s += count;
    while (count--) *--d = *--s;
    return dst;
}

int memcmp(const void *a, const void *b, uint32 count)
{
    const uint8 *pa = (const uint8 *)a;
    const uint8 *pb = (const uint8 *)b;
    while (count--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

/* ======================================================================
 * String functions
 * ====================================================================== */

uint32 strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (uint32)(p - s);
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0');
    return dst;
}

/*
 * strncpy: copy at most n bytes; if src is shorter than n, pad remainder
 * with NUL bytes.
 *
 * Fixed: uses indexed loop to avoid the ambiguous while(n--)
 * post-decrement counting issue in the original.
 */
char *strncpy(char *dst, const char *src, uint32 n)
{
    uint32 i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') break;
    }
    /* NUL-pad remaining positions */
    for (i++; i < n; i++) dst[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(uint8)*a - (int)(uint8)*b;
}

int strncmp(const char *a, const char *b, uint32 n)
{
    if (n == 0) return 0;
    while (--n && *a && (*a == *b)) { a++; b++; }
    return (int)(uint8)*a - (int)(uint8)*b;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != '\0');
    return dst;
}

char *strncat(char *dst, const char *src, uint32 n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : nullptr;
}

char *strrchr(const char *s, int c)
{
    char        ch   = (char)c;
    const char *last = nullptr;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0') return (char *)haystack;
    uint32 nlen = strlen(needle);
    while (*haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
        haystack++;
    }
    return nullptr;
}

/* ======================================================================
 * Conversion functions
 * ====================================================================== */

int atoi(const char *s)
{
    int result = 0;
    int sign   = 1;
    while (isspace(*s)) s++;
    if      (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (isdigit(*s)) { result = result * 10 + (*s - '0'); s++; }
    return result * sign;
}

static void reverse_buf(char *buf, uint32 len)
{
    char *a = buf, *b = buf + len - 1;
    while (a < b) { char t = *a; *a++ = *b; *b-- = t; }
}

char *itoa(int value, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    if (base < 2 || base > 16) { buf[0] = '\0'; return buf; }

    char *p = buf;
    bool  neg = false;

    if (value < 0 && base == 10) {
        neg = true;
        uint32 uval = (uint32)(-(value + 1)) + 1u;
        do { *p++ = digits[uval % (uint32)base]; uval /= (uint32)base; } while (uval);
    } else {
        uint32 uval = (uint32)value;
        do { *p++ = digits[uval % (uint32)base]; uval /= (uint32)base; } while (uval);
    }
    if (neg) *p++ = '-';
    *p = '\0';
    reverse_buf(buf, (uint32)(p - buf));
    return buf;
}

char *utoa(uint32 value, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    if (base < 2 || base > 16) { buf[0] = '\0'; return buf; }
    char *p = buf;
    do { *p++ = digits[value % (uint32)base]; value /= (uint32)base; } while (value);
    *p = '\0';
    reverse_buf(buf, (uint32)(p - buf));
    return buf;
}

/* ======================================================================
 * Character classification and conversion
 * ====================================================================== */

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int isdigit (int c) { return (c >= '0' && c <= '9'); }
int isalpha (int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int isalnum (int c) { return isdigit(c) || isalpha(c); }
int isspace (int c) { return c == ' ' || c == '\t' || c == '\n' ||
                             c == '\r' || c == '\f' || c == '\v'; }
int isprint (int c) { return c >= 0x20 && c <= 0x7E; }
int isupper (int c) { return c >= 'A' && c <= 'Z'; }
int islower (int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) ||
                             (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F'); }
int iscntrl (int c) { return (uint32)c < 0x20 || c == 0x7F; }
int ispunct (int c) { return isprint(c) && !isalnum(c) && c != ' '; }

} /* extern "C" */
