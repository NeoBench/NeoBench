#ifndef NBFH_API68K_H
#define NBFH_API68K_H

/* exec.library glue for the 68k handler build (NBFH_AMIGA).
 * Implemented in 68ksvc.S; only declarations live here.  Not usable on
 * the host build. */

#if !defined(NBFH_AMIGA) || !defined(__m68k__)
#error api68k.h is for the 68k Amiga build only
#endif

#include <stdint.h>

#define EXECF_MEMF_PUBLIC  (1u << 16)
#define EXECF_MEMF_CLEAR   (1u << 1)

#define EXEC_CMD_READ      2
#define EXEC_CMD_UPDATE    3

/* exec IORequest (offsets match exec_lib, 32-bit ABI) */
struct nbfh_ioreq
{
    uint32_t node[5];     /* 0  Message mn_Node       */
    void *reply_port;     /* 20 Message mn_ReplyPort  */
    uint16_t msg_len;     /* 24 Message mn_Length     */
    uint16_t pad0;        /* 26                       */
    void *device;         /* 28                       */
    void *unit;           /* 32                       */
    uint16_t command;     /* 36                       */
    uint8_t flags;        /* 38                       */
    uint8_t error;        /* 39                       */
    uint32_t actual;      /* 40                       */
    uint32_t len;         /* 44                       */
    void *data;           /* 48                       */
    uint32_t offset;      /* 52                       */
    uint32_t reserved;    /* 56                       */
    uint32_t flags2;      /* 60                       */
    uint32_t three;       /* 64                       */
};

void *nbh_alloc(void *sysbase, uint32_t size, uint32_t flags);
void nbh_free(void *sysbase, void *p, uint32_t size);
void *nbh_create_msgport(void *sysbase);
void nbh_delete_msgport(void *sysbase, void *port);
void nbh_waitport(void *sysbase, void *port);
void *nbh_getmsg(void *sysbase, void *port);
void nbh_replymsg(void *sysbase, void *msg);
void *nbh_create_iorequest(void *sysbase, void *port, uint32_t size);
void nbh_delete_iorequest(void *sysbase, void *req);
int32_t nbh_open_device(void *sysbase, const char *name, uint32_t unit,
                        void *ioreq, uint32_t flags);
void nbh_doio(void *sysbase, void *ioreq);

#endif /* NBFH_API68K_H */