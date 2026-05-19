/*
 * NeoBench Kernel - kprintf / ksprintf
 * Bare-metal Amiga 68030/040/060
 *
 * Minimal printf implementation for kernel use.
 * Outputs through neo::display::putchar() or to a memory buffer.
 *
 * Corrections vs v1.0:
 *
 *  1. %l MODIFIER READS 32-BIT ARGUMENT (silent truncation on 64-bit values).
 *     The original noted "on M68K int and long are both 32 bits" and discarded
 *     is_long entirely.  This is correct for int/long, but %lld / %llu (%ll)
 *     would need a 64-bit path.  We don't add %ll but we do handle %l cleanly
 *     so that 'long' arguments don't generate undefined-behaviour VA reads.
 *     On 68k: int = long = 32 bits, so %d and %ld are identical. No change
 *     needed for values, but we add a comment to make this explicit.
 *
 *  2. %p DOES NOT ACCOUNT FOR "0x" IN WIDTH CALCULATION.
 *     The original emitted "0x" then padded the hex digits to 8 chars.
 *     If a width was specified (e.g. %12p) the "0x" prefix was not counted
 *     against the width, so the total output would be 10 chars (2+8) instead
 *     of the requested 12.  We now count "0x" as part of the total width.
 *
 *  3. ZERO_PAD AND LEFT_JUSTIFY INTERACTION.
 *     POSIX says: if '-' (left justify) is specified, '0' (zero pad) is
 *     ignored.  The original computed fill = '0' when zero_pad=true even
 *     if left_justify=true, then used fill for the right-side pad in the
 *     left-justified branch.  This produced "123   " correctly (fill is
 *     only applied for the left-pad in right-justify mode), so in practice
 *     the bug did not manifest.  However the fill variable was computed
 *     ambiguously.  Clarified: fill is only relevant for right-justified
 *     numeric padding; left-justified always pads with spaces on the right.
 *
 *  4. ksprintf BUF_SIZE=0 DEREFERENCES buf.
 *     If buf_size == 0 is passed to ksprintf, the NUL-termination code
 *     does:
 *       if (st.buf_size > 0) *st.buf = '\0';
 *       else if (buf_size > 0) buf[buf_size - 1] = '\0';
 *     When buf_size=0, both branches are skipped, which is correct.
 *     But the emit() function checks "if (st.buf_size > 1)" before writing,
 *     which also correctly rejects writes when buf_size <= 1.
 *     No crash, but count is still incremented (correct - returns would-be
 *     length, like snprintf).  No change needed; verified correct.
 *
 *  5. MISSING %s PRECISION SPECIFIER.
 *     The original had no '.' precision parsing.  "%.10s" would be treated
 *     as "%s" (precision ignored), possibly printing more than intended.
 *     Added basic precision parsing: for %s, precision limits the number
 *     of characters printed.
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "string.h"

namespace {

/* -----------------------------------------------------------------------
 * Output abstraction - either to display or to a memory buffer
 * ----------------------------------------------------------------------- */
struct PrintState {
    char   *buf;        /* nullptr  -> display output via putchar   */
    uint32  buf_size;   /* remaining capacity (for ksprintf)        */
    uint32  count;      /* total characters written so far          */
};

static void emit(PrintState &st, char c)
{
    if (st.buf) {
        if (st.buf_size > 1) {          /* always reserve room for NUL */
            *st.buf++ = c;
            st.buf_size--;
        }
    } else {
        neo::display::putchar(c);
    }
    st.count++;
}

static void emit_string(PrintState &st, const char *s, uint32 len)
{
    for (uint32 i = 0; i < len; i++) emit(st, s[i]);
}

/* -----------------------------------------------------------------------
 * Pad helper
 * ----------------------------------------------------------------------- */
static void pad(PrintState &st, char fill, int count)
{
    while (count-- > 0) emit(st, fill);
}

/* -----------------------------------------------------------------------
 * Unsigned integer to string
 * Returns the number of characters written into out (no NUL).
 * out must be at least 34 bytes (binary 32-bit worst case).
 * ----------------------------------------------------------------------- */
static const char kDigitsLower[] = "0123456789abcdef";
static const char kDigitsUpper[] = "0123456789ABCDEF";

static int fmt_uint(char *out, uint32 value, int base, bool upper)
{
    const char *digits = upper ? kDigitsUpper : kDigitsLower;
    char tmp[34];
    int  len = 0;

    do {
        tmp[len++] = digits[value % (uint32)base];
        value /= (uint32)base;
    } while (value);

    for (int i = 0; i < len; i++) out[i] = tmp[len - 1 - i];
    return len;
}

/* -----------------------------------------------------------------------
 * Core formatter
 * ----------------------------------------------------------------------- */
static void kvformat(PrintState &st, const char *fmt, __builtin_va_list ap)
{
    while (*fmt) {
        if (*fmt != '%') { emit(st, *fmt++); continue; }
        fmt++; /* skip '%' */

        /* ---- flags ---- */
        bool left_justify = false;
        bool zero_pad     = false;
        bool show_plus    = false;

        for (;;) {
            if      (*fmt == '-') { left_justify = true; fmt++; }
            else if (*fmt == '0') { zero_pad     = true; fmt++; }
            else if (*fmt == '+') { show_plus    = true; fmt++; }
            else break;
        }

        /* POSIX: '0' flag is ignored when '-' is specified */
        if (left_justify) zero_pad = false;

        /* ---- field width ---- */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* ---- precision ---- */
        int precision = -1; /* -1 means not specified */
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* ---- length modifier ---- */
        /* On 68k: int == long == 32 bits, so 'l' is a no-op for integer
         * conversions.  We parse it to avoid treating it as a conversion. */
        bool is_long = false;
        if (*fmt == 'l') { is_long = true; fmt++; }
        (void)is_long;

        /* ---- conversion ---- */
        char  numbuf[34];
        int   numlen = 0;
        bool  negative = false;
        char  fill = zero_pad ? '0' : ' ';

        switch (*fmt) {
        case '\0':
            return;

        case '%':
            emit(st, '%');
            break;

        /* ---- character ---- */
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            if (!left_justify) pad(st, ' ', width - 1);
            emit(st, c);
            if ( left_justify) pad(st, ' ', width - 1);
            break;
        }

        /* ---- string ---- */
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            uint32 slen = strlen(s);
            /* Apply precision as maximum length */
            if (precision >= 0 && (uint32)precision < slen)
                slen = (uint32)precision;
            int padding = width - (int)slen;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, ' ', padding);
            emit_string(st, s, slen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- signed decimal ---- */
        case 'd':
        case 'i': {
            int32 val = (int32)__builtin_va_arg(ap, int);
            uint32 uval;
            if (val < 0) {
                negative = true;
                uval = (uint32)(-(val + 1)) + 1u;
            } else {
                uval = (uint32)val;
            }
            numlen = fmt_uint(numbuf, uval, 10, false);

            int sign_chars = (negative || show_plus) ? 1 : 0;
            int total      = numlen + sign_chars;
            int padding    = width - total;
            if (padding < 0) padding = 0;

            if (!left_justify) {
                if (zero_pad) {
                    if (negative)        emit(st, '-');
                    else if (show_plus)  emit(st, '+');
                    pad(st, '0', padding);
                } else {
                    pad(st, ' ', padding);
                    if (negative)        emit(st, '-');
                    else if (show_plus)  emit(st, '+');
                }
            } else {
                if (negative)        emit(st, '-');
                else if (show_plus)  emit(st, '+');
            }
            emit_string(st, numbuf, (uint32)numlen);
            if (left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- unsigned decimal ---- */
        case 'u': {
            uint32 val = __builtin_va_arg(ap, uint32);
            numlen = fmt_uint(numbuf, val, 10, false);
            int padding = width - numlen;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, fill, padding);
            emit_string(st, numbuf, (uint32)numlen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- hex lower ---- */
        case 'x': {
            uint32 val = __builtin_va_arg(ap, uint32);
            numlen = fmt_uint(numbuf, val, 16, false);
            int padding = width - numlen;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, fill, padding);
            emit_string(st, numbuf, (uint32)numlen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- hex upper ---- */
        case 'X': {
            uint32 val = __builtin_va_arg(ap, uint32);
            numlen = fmt_uint(numbuf, val, 16, true);
            int padding = width - numlen;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, fill, padding);
            emit_string(st, numbuf, (uint32)numlen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- octal ---- */
        case 'o': {
            uint32 val = __builtin_va_arg(ap, uint32);
            numlen = fmt_uint(numbuf, val, 8, false);
            int padding = width - numlen;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, fill, padding);
            emit_string(st, numbuf, (uint32)numlen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        /* ---- pointer ---- */
        case 'p': {
            uint32 val = (uint32)__builtin_va_arg(ap, void *);
            numlen = fmt_uint(numbuf, val, 16, false);
            /* Pad hex digits to 8 (32-bit pointer = 8 hex digits) */
            int hex_width  = numlen < 8 ? 8 : numlen;
            int total_width = hex_width + 2; /* include "0x" */
            int padding = width - total_width;
            if (padding < 0) padding = 0;
            if (!left_justify) pad(st, ' ', padding);
            emit(st, '0'); emit(st, 'x');
            pad(st, '0', hex_width - numlen);
            emit_string(st, numbuf, (uint32)numlen);
            if ( left_justify) pad(st, ' ', padding);
            break;
        }

        default:
            /* Unknown specifier - emit literally */
            emit(st, '%');
            emit(st, *fmt);
            break;
        }

        fmt++;
    }
}

} /* anonymous namespace */

/* ======================================================================
 * Public API (C linkage - callable from C and assembly)
 * ====================================================================== */

extern "C" {

int kprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    PrintState st { nullptr, 0, 0 };
    kvformat(st, fmt, ap);

    __builtin_va_end(ap);
    return (int)st.count;
}

int ksprintf(char *buf, uint32 buf_size, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    PrintState st { buf, buf_size, 0 };
    kvformat(st, fmt, ap);

    /* NUL-terminate: st.buf points one past the last written character */
    if (buf && buf_size > 0) {
        if (st.buf_size > 0)
            *st.buf = '\0';         /* Normal case: room remains */
        else
            buf[buf_size - 1] = '\0'; /* Buffer filled exactly: clobber last */
    }

    __builtin_va_end(ap);
    return (int)st.count;          /* Returns total chars (like snprintf) */
}

} /* extern "C" */
