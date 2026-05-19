/*
 * NeoBench Kernel - String / Memory Implementation (C99)
 * Bare-metal Amiga 68030/040/060
 * Refactored for VBCC/C99 compatibility
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include "string.h"

/* Helper for itoa/utoa */
static void reverse_buf(char *buf, uint32 len)
{
    char *a = buf, *b = buf + len - 1;
    while (a < b) { char t = *a; *a++ = *b; *b-- = t; }
}

/* Memory functions */
void *memset(void *dst, int val, uint32 count)
{
    uint8 *d = (uint8 *)dst;
    uint8  v = (uint8)val;
    while (count--) *d++ = v;
    return dst;
}

void *memcpy(void *dst, const void *src, uint32 count)
{
    uint8       *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;
    while (count--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, uint32 count)
{
    uint8       *d = (uint8 *)dst;
    const uint8 *s = (const uint8 *)src;
    if (d == s || count == 0) return dst;
    if (d < s || d >= s + count) return memcpy(dst, src, count);
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

/* String functions */
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

char *strncpy(char *dst, const char *src, uint32 n)
{
    uint32 i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') break;
    }
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
    return (ch == '\0') ? (char *)s : (char *)0;
}

char *strrchr(const char *s, int c)
{
    char        ch   = (char)c;
    const char *last = (const char *)0;
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
    return (char *)0;
}

/* Conversion functions */
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

char *itoa(int value, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    if (base < 2 || base > 16) { buf[0] = '\0'; return buf; }

    char *p = buf;
    int neg = 0;

    if (value < 0 && base == 10) {
        neg = 1;
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

/* Character classification */
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int isdigit (int c) { return (c >= '0' && c <= '9'); }
int isalpha (int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int isalnum (int c) { return isdigit(c) || isalpha(c); }
int isspace (int c) { return c == ' ' || c == '	' || c == '
' ||
                             c == '' || c == '\f' || c == '\v'; }
int isprint (int c) { return c >= 0x20 && c <= 0x7E; }
int isupper (int c) { return c >= 'A' && c <= 'Z'; }
int islower (int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) ||
                             (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F'); }
int iscntrl (int c) { return (uint32)c < 0x20 || c == 0x7F; }
int ispunct (int c) { return isprint(c) && !isalnum(c) && c != ' '; }
