/*
 * NeoBench Kernel - String / Memory Function Declarations
 * Bare-metal Amiga 68030/040/060
 *
 * All functions implemented in lib/string.cpp.
 * No libc dependency.
 *
 * Corrections vs v1.0:
 *   - Added isalnum, isxdigit, isupper, islower, iscntrl, ispunct
 *     declarations to match implementations added in string.cpp.
 */

#ifndef NEOBENCH_STRING_H
#define NEOBENCH_STRING_H

#include "../include/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Memory functions
 * ----------------------------------------------------------------------- */

void  *memset (void *dst, int val, uint32 count);
void  *memcpy (void *dst, const void *src, uint32 count);
void  *memmove(void *dst, const void *src, uint32 count);
int    memcmp (const void *a, const void *b, uint32 count);

/* -----------------------------------------------------------------------
 * String functions
 * ----------------------------------------------------------------------- */

uint32 strlen (const char *s);
char  *strcpy (char *dst, const char *src);
char  *strncpy(char *dst, const char *src, uint32 n);
int    strcmp (const char *a, const char *b);
int    strncmp(const char *a, const char *b, uint32 n);
char  *strcat (char *dst, const char *src);
char  *strncat(char *dst, const char *src, uint32 n);
//const char  *strchr (const char *s, int c);
//const char  *strrchr(const char *s, int c);
//const char  *strstr (const char *haystack, const char *needle);

/* -----------------------------------------------------------------------
 * Conversion functions
 * ----------------------------------------------------------------------- */

int    atoi(const char *s);
char  *itoa(int value,    char *buf, int base);
char  *utoa(uint32 value, char *buf, int base);

/* -----------------------------------------------------------------------
 * Character classification
 * ----------------------------------------------------------------------- */

int isdigit (int c);
int isalpha (int c);
int isalnum (int c);
int isspace (int c);
int isprint (int c);
int isupper (int c);
int islower (int c);
int isxdigit(int c);
int iscntrl (int c);
int ispunct (int c);

/* -----------------------------------------------------------------------
 * Character conversion
 * ----------------------------------------------------------------------- */

int toupper(int c);
int tolower(int c);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NEOBENCH_STRING_H */
