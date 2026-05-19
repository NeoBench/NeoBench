#ifndef __STDIO_H
#define __STDIO_H 1

#ifndef __VCLIB_SIMPLEIO
#define __VCLIB_SIMPLEIO 1
#endif

#define EOF (-1)

#ifndef BUFSIZ
#define BUFSIZ 2
#endif
#define FOPEN_MAX 1

#define _IOFBF 1L
#define _IOLBF 2L
#define _IONBF 3L

#define HASBUF 1L
#define NOBUFMEM 2L

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#define _READ 1
#define _WRITE 2
#define _UNBUF 0
#define _EOF 4
#define _ERR 128
#define _READABLE 8
#define _WRITEABLE 16
#define _LINEBUF 0
#define _NOTMYBUF 32
#define _ISTTY 64
/* Define this for CRLF-conversion in text-mode
#define _CLE 16384
*/

#ifdef __BUILD_LIB
#define HAVE_TTYS 1
#include "libsys.h"
#ifdef HAVE_TTYS
extern int __isatty(int);
#endif
#endif

#ifdef __VCLIB_SIMPLEIO
typedef struct _iobuf
{
  unsigned char filehandle;
  unsigned char flags;
  signed char count;
  unsigned char bufsize;
  char *pointer;
  char *base;                 /*  buffer address  */
  /*struct _iobuf *next;*/
} FILE;
#else
typedef struct _iobuf
{
    int   filehandle;           /*  filehandle  */
    char *pointer;
    char *base;                 /*  buffer address  */
    struct _iobuf *next;
    struct _iobuf *prev;
    int count;
    int flags;
    int bufsize;
#ifdef _CLE
    int clebuf;                 /* buffered char during CRLF conversion */
#endif
} FILE;
#endif

#if __BUILD_LIB
#if MASK_STDIO
extern __mask(1) FILE *stdin, *stdout, *stderr;
#define stdin __maskm_1_stdin
#define stdout __maskm_1_stdout
#define stderr __maskm_1_stderr
#define __read __maskm_1___read
#define __write __maskm_1___write
#else
extern FILE *stdin, *stdout, *stderr;
#endif
#else /* __BUILD_LIB */
extern __mask(1) FILE *stdin, *stdout, *stderr;
#define stdin __maskm_1_stdin
#define stdout __maskm_1_stdout
#define stderr __maskm_1_stderr
extern __mask(1) fflush(FILE *);
#define fflush(x) __maskm_1_fflush(x)
#endif

int _fillbuf(FILE *),_putbuf(int,FILE *),_flushbuf(FILE *);
void _ttyflush(void);

#define L_tmpnam        16
#define TMP_MAX FOPEN_MAX

#define FILENAME_MAX 16

/*
  Adapt corresponding to stddef.h
*/
#ifndef __SIZE_T
#define __SIZE_T 1
#ifdef __SIZE_T_INT
typedef unsigned int size_t;
#else
typedef unsigned long size_t;
#endif
#endif

/*
  Adapt as needed.
*/
#ifndef __FPOS_T
#define __FPOS_T 1
typedef int fpos_t;
#endif

#ifndef __STDARG_H
#include <stdarg.h>
#endif

#undef NULL
#define NULL ((void*)0)

FILE *fopen(const char *,const char *);
FILE *freopen(const char *,const char *,FILE *);
int fflush(FILE *);
int fclose(FILE *);
int rename(const char *,const char *);
int remove(const char *);
FILE *tmpfile(void);
char *tmpnam(char *);
int setvbuf(FILE *,char *,int,size_t);
void setbuf(FILE *,char *);
int fprintf(FILE *, const char *, ...);
int printf(const char *, ...);
int sprintf(char *, const char *,...);
int snprintf(char *,size_t,const char *,...);
/*
  Simple versions of IO functions (see vbcc documentation).
  If versions with __v1 or __v2 are declared they are also used.
*/
int vprintf(const char *,va_list);
int vfprintf(FILE *,const char *,va_list);
int vsprintf(char *,const char *,va_list);
int vsnprintf(char *,size_t,const char *,va_list);
int fscanf(FILE *, const char *, ...);
int scanf(const char *, ...);
int sscanf(const char *, const char *, ...);
int vscanf(const char *,va_list);
int vfscanf(FILE *,const char *,va_list);
int vsscanf(const char *,const char *,va_list);
char *fgets(char *, int, FILE *);
int fputs(const char *, FILE *);
char *gets(char *);
int puts(const char *);
int ungetc(int,FILE *);
size_t fread(void *,size_t,size_t,FILE *);
size_t fwrite(void *,size_t,size_t,FILE *);
int fseek(FILE *,long,int);
void rewind(FILE *);
long ftell(FILE *);
int fgetpos(FILE *,fpos_t *);
int fsetpos(FILE *,const fpos_t *);
void perror(const char *);
int fgetc(FILE *);
int fputc(int,FILE *);
int getchar(void);
int putchar(int);
#ifndef __VCLIB_SIMPLEIO
#if defined(_CLE)
int __putc(int,FILE *);
int __getc(FILE *);
#else
int __putc(int,FILE *);
int __getc(FILE *);
#define __putc(x,f) __rawputc((x),(f))
#define __getc(f) __rawgetc(f)
#endif
#else
int __putc(int,FILE *);
int __getc(FILE *);
#endif


#define __rawputc(x,p) (((p)->flags|=_WRITE),((--((FILE*)(p))->count>=0&&((x)!='\n'||!(((FILE*)(p))->flags&_LINEBUF)))?(unsigned char)(*((FILE*)(p))->pointer++=(x)):_putbuf((x),p)))
#define putc(x,f) __putc((x),(f))
#define putchar(x) __putc((x),stdout)
#define __rawgetc(p) (__check((p),FILE*),((p)->flags|=_READ),--((FILE*)(p))->count>=0?(unsigned char)*((FILE*)(p))->pointer++:_fillbuf(p))
#define getc(f) __getc(f)
#define getchar() __getc(stdin)

#define __check(arg,type) (volatile)sizeof((arg)==(type)0)

#define feof(p)         (__check((p),FILE*),((FILE*)(p))->flags&_EOF)
#define ferror(p)       (__check((p),FILE*),((FILE*)(p))->flags&_ERR)
#define clearerr(p)     (__check((p),FILE*),((FILE*)(p))->flags&=~(_ERR|_EOF))

#define fsetpos(f,ptr)  fseek((f),*(ptr),SEEK_SET)

#ifdef __VBCC__
#pragma printflike printf
#pragma printflike fprintf
#pragma printflike sprintf
#pragma printflike snprintf
#pragma scanflike scanf
#pragma scanflike fscanf
#pragma scanflike sscanf
#endif

#endif
