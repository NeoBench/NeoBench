#ifndef NBH_DISPATCH_H
#define NBH_DISPATCH_H

#include <stdint.h>
#include "vol.h"

/*
 * Platform-independent dos packet dispatch.  The Amiga side allocates a
 * normalized packet (decoded BADDR args) and calls nbh_dispatch(); the host
 * test builds the same struct with native pointers.
 *
 * `nbh_ptr` carries an argument value: an absolute (FAST) pointer on the
 * Amiga build (see DOSDrivers/nbfs_handler startup math) and a native
 * pointer in the host test.
 */

#ifdef NBH_AMIGA
typedef uint32_t nbh_ptr;
#define NBH_PTR(p)     ((void *)(uintptr_t)(p))
#define NBH_PTR_LONG(p) ((uint32_t)(uintptr_t)(p))
#else
typedef void *nbh_ptr;
#define NBH_PTR(p)     (p)
#define NBH_PTR_LONG(p) ((uint32_t)(uintptr_t)(p))
#endif

/* FileLock (subset, absolute layout, 32-bit words). */
#define NBH_FL_SIZE 48u
struct nbh_filelock
{
    uint32_t fl_Link;
    int32_t  fl_Access;
    uint32_t fl_Task;   /* our port */
    uint32_t fl_Volume;
    uint32_t fl_Arg1;   /* NBFS: for dirs, scan counter (next ordinal) */
    int32_t  fl_Pos;
    uint32_t fl_Key;    /* NBFS inode number */
    uint32_t fl_Date[3];
    uint32_t fl_Handle;
};

/* FileHandle (subset). */
struct nbh_fh
{
    uint32_t fh_Link;
    uint32_t fh_Port;
    int32_t  fh_Type;
    int32_t  fh_Buf;
    int32_t  fh_Pos;    /* file position */
    int32_t  fh_End;
    int32_t  fh_Funcs;
    uint32_t fh_Arg1;   /* NBFS inode number */
    uint32_t fh_Buf2;
    uint32_t fh_BufExp;
};

/* FileInfoBlock (subset, absolute layout). */
#define NBH_FIB_FILENAME_OFF  16u
#define NBH_FIB_SIZE         512u
struct nbh_fib
{
    uint32_t fib_DiskKey;
    int32_t  fib_DirEntryType;
    uint8_t  fib_FileName[108];   /* BSTR */
    int32_t  fib_Protection;
    int32_t  fib_EntryType;
    int32_t  fib_Size;
    int32_t  fib_NumBlocks;
    uint32_t fib_Date[3];
    uint8_t  fib_Comment[80];     /* BSTR */
    uint8_t  fib_Owner[4];
    uint8_t  fib_Reserved[8];
    uint8_t  fib_Reserved2[264];
};

/* InfoData (subset, absolute layout). */
struct nbh_info
{
    uint32_t id_NumSoftErrors;
    uint32_t id_UnitNumber;
    int32_t  id_DiskState;
    int32_t  id_NumBlocks;
    int32_t  id_NumBlocksUsed;
    int32_t  id_BytesPerBlock;
    int32_t  id_DiskType;
    uint8_t  id_VolumeNode;
    int32_t  id_InUse;
    uint8_t  id_VolumeName[36];    /* BSTR */
    uint8_t  id_Reserved[36];
};

/* --- dos packets (subset; values from dos/dosextens.h) --- */
#define ACTION_STARTUP         0
#define ACTION_DISK_INFO       1
#define ACTION_OPEN            3
#define ACTION_CLOSE           4
#define ACTION_READ            5
#define ACTION_WRITE           6
#define ACTION_SEEK            8
#define ACTION_FINDINPUT       9
#define ACTION_FINDOUTPUT      10
#define ACTION_FREE_LOCK       15
#define ACTION_LOCATE_OBJECT   16
#define ACTION_CURRENT_DIR     27
#define ACTION_PARENT          29
#define ACTION_INHIBIT         32
#define ACTION_EXAMINE_OBJECT  23
#define ACTION_EXAMINE_NEXT    24
#define ACTION_GET_DISK_INFO   47

#define DOSTRUE   (-1)
#define DOSFALSE  (0)

/* Errors are negative res2 values (dos errors are positive longs stored
 * negated); values from dos.h. */
#define ERROR_NO_MORE_ENTRIES    103
#define ERROR_OBJECT_NOT_FOUND    - 200

/* Normalized packet passed to the dispatcher. */
struct nbh_pkt
{
    uint32_t type;
    nbh_ptr  arg1, arg2, arg3, arg4, arg5, arg6;
};

/* Callbacks back into the Amiga glue (host stub implements trivially). */
struct nbh_host
{
    int (*alloc_lock)(struct nbh_filelock **out, uint32_t ino, int32_t access);
    void (*free_lock)(struct nbh_filelock *fl);
};

extern uint32_t nbh_volume_name_len;
extern const char *nbh_volume_name;

struct nbh_vol *nbh_mainvol(void);

/* Returns res1 in *res1, res2 in *res2. */
void nbh_dispatch(struct nbh_pkt *pkt, long *res1, long *res2);

#endif /* NBH_DISPATCH_H */