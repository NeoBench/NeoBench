#include <stddef.h>

/* map to OS-9 functions */
#include <os9.h>
#define __read(f,b,s) read(f,b,s)
#define __write(f,b,s) write(f,b,s)
#define __creat(n,m) creat(n,m)
#define __open(n,m) open(n,m)
#define __close(f) close(f)
#define __seek(f,p,w) lseek(f,p,w)

void _chkabort();
