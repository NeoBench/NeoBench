#ifndef NBFH_DOSPCK_H
#define NBFH_DOSPCK_H

#include <stdint.h>
#include "vol.h"

/* Host builds widen the packet fields so test pointers round-trip; the
 * real 68k build uses the native 32-bit packet ABI. */
#ifdef NBFH_HOST
typedef uintptr_t nbfh_arg_t;
typedef intptr_t  nbfh_res1_t;
typedef uintptr_t nbfh_res2_t;
#else
typedef uint32_t nbfh_arg_t;
typedef int32_t  nbfh_res1_t;
typedef uint32_t nbfh_res2_t;
#endif

/* On the Amiga, packet args are by convention either BCPL pointers
 * (BPTR, so >> 2 / << 2) or real pointers for buffers.  On the host
 * everything is the real pointer. */
#ifdef NBFH_AMIGA
#define NBH_BENC(p)     ((uintptr_t)(p) >> 2)
#define NBH_BDEC(v)     ((void *)(uintptr_t)((v) << 2))
#define NBH_PENC(p)     ((uintptr_t)(p))
#define NBH_PDEC(v)     ((void *)(uintptr_t)(v))
#else
#define NBH_BENC(p)     ((uintptr_t)(p))
#define NBH_BDEC(v)     ((void *)(uintptr_t)(v))
#define NBH_PENC(p)     ((uintptr_t)(p))
#define NBH_PDEC(v)     ((void *)(uintptr_t)(v))
#endif

#define ST_ROOT           1
#define ST_USERDIR        2
#define ST_SOFTLINK       3
#define ST_LINKDIR        4
#define ST_FILE           (-3)
#define ST_LINKFILE       (-4)

#define ACCESS_READ       (-2)
#define ACCESS_WRITE      (-1)

#define OFFSET_BEGINNING  (-1)
#define OFFSET_CURRENT    0
#define OFFSET_END        1

#define DOSTRUE           (-1)

#define ID_WRITE_PROTECTED 80
#define ID_VALIDATED       82

#define FIBB_DELETE        0
#define FIBB_EXECUTE       1
#define FIBB_WRITE         2
#define FIBB_READ          3
#define FIBF_DELETE        (1u << FIBB_DELETE)
#define FIBF_EXECUTE       (1u << FIBB_EXECUTE)
#define FIBF_WRITE         (1u << FIBB_WRITE)
#define FIBF_READ          (1u << FIBB_READ)

#define ACTION_NIL               0
#define ACTION_STARTUP           0
#define ACTION_SET_MAP           4
#define ACTION_DIE               5
#define ACTION_EVENT             6
#define ACTION_CURRENT_VOLUME    7
#define ACTION_LOCATE_OBJECT     8
#define ACTION_RENAME_DISK       9
#define ACTION_WRITE             'W'
#define ACTION_READ              'R'
#define ACTION_FREE_LOCK         15
#define ACTION_SAME_LOCK         40
#define ACTION_EXAMINE_OBJECT    23
#define ACTION_EXAMINE_NEXT      24
#define ACTION_DISK_INFO         25
#define ACTION_INFO              26
#define ACTION_FLUSH             27
#define ACTION_PARENT            29
#define ACTION_INHIBIT           31
#define ACTION_SEEK              1008
#define ACTION_FINDUPDATE        1004
#define ACTION_FINDINPUT         1005
#define ACTION_FINDOUTPUT        1006
#define ACTION_END               1007
#define ACTION_PASSWORD          1009
#define ACTION_FH_FROM_LOCK      1026
#define ACTION_IS_FILESYSTEM     1027
#define ACTION_EXAMINE_ALL       1033
#define ACTION_EXAMINE_FH        1034

#define ERROR_NO_FREE_STORE        103
#define ERROR_OBJECT_IN_USE        202
#define ERROR_OBJECT_EXISTS        203
#define ERROR_OBJECT_NOT_FOUND     205
#define ERROR_OBJECT_WRONG_TYPE    206
#define ERROR_ACTION_NOT_KNOWN     209
#define ERROR_INVALID_LOCK         211
#define ERROR_DEVICE_NOT_MOUNTED   218
#define ERROR_SEEK_ERROR           219
#define ERROR_WRITE_PROTECTED      223
#define ERROR_READ_PROTECTED       224
#define ERROR_NOT_A_DOS_DISK       225
#define ERROR_NO_MORE_ENTRIES      232

struct nbfh_pkt
{
    uint32_t link;
    uint32_t port;
    uint32_t type;
    nbfh_res1_t res1;
    nbfh_res2_t res2;
    nbfh_arg_t arg[7];
};

struct nbfh_fib
{
    uint32_t diskkey;
    int32_t dirdtype;
    uint8_t filename_len;
    char filename[NBH_DIRENTRY_MAXNAME - 1];
    uint32_t protection;
    int32_t entrytype;
    uint32_t fsize;
    uint32_t numblocks;
    uint32_t date[3];
    uint8_t comment_len;
    char comment[79];
    uint16_t owner_uid;
    uint16_t owner_gid;
    uint32_t reserved[8];
};

struct nbfh_lock
{
    uint32_t link;
    uint32_t key;
    int32_t access;
    uint32_t task;
    uint32_t volume;
};

struct nbfh_handle
{
    uint32_t link;
    uint32_t port;
    uint32_t type;
    uint32_t buf;
    int32_t pos;
    int32_t end;
    uint32_t funcs;
    uint32_t func2;
    uint32_t func3;
    uint32_t arg1;
    uint32_t arg2;
};

struct nbfh_info
{
    uint32_t num_soft_errors;
    int32_t unit_number;
    int32_t disk_state;
    uint32_t num_blocks;
    uint32_t num_blocks_used;
    int32_t bytes_per_block;
    uint32_t disk_type;
    uint32_t volume_node;
    uint32_t in_use;
};

#define NBFH_FIB_SIZE      260
#define NBFH_LOCK_SIZE     32
#define NBFH_HANDLE_SIZE   44
#define NBFH_INFO_SIZE     36

#define NBFS_DISKTYPE      0x4E424653u

struct nbh_rt
{
    void *ud;
    uint32_t handler_port;
    void *(*alloc)(void *ud, uint32_t size);
    void (*free)(void *ud, void *p, uint32_t size);
    void (*reply)(void *ud, struct nbfh_pkt *pkt);
};

void nbh_serve(struct nbh_vol *vol, struct nbh_rt *rt, struct nbfh_pkt *pkt);

#endif