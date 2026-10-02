#ifndef NB_ISO9660_H
#define NB_ISO9660_H

#include <stdint.h>

/*
 * ISO 9660 -- the filesystem on the install disc, read through
 * atapi.device by NeoBench's own code.
 *
 * The disc carries NeoBench's boot block in its system area: sector 0
 * starts "NBISO", and that is what separates an installer disc from
 * any other ISO 9660 volume that might be in the drive.  The volume
 * itself is the standard one at sector 16, so a mount result answers
 * three different questions at once -- is there a disc, is it ISO 9660,
 * and is it NeoBench's -- rather than leaving a caller to re-derive
 * them from failure modes.
 *
 * Everything is read a sector at a time through the device table by
 * name, which is what lets a host test stand a fake CD in place of
 * real hardware (tools/tests/test_iso9660.c) without this file
 * knowing it happened.
 */

/* nb_iso_mount() results: none is not a failure, it is an answer. */
#define NB_ISO_NONE     0   /* no ISO 9660 volume on the disc       */
#define NB_ISO_DISC     1   /* someone else's ISO 9660 disc         */
#define NB_ISO_INSTALL  2   /* NeoBench's installer boot block seen */

/*
 * Mount the disc in atapi.device unit 0 and return one of the three
 * results above.  Reads the boot block and the volume descriptor,
 * records the volume identity and counts the root directory, so a
 * caller that only wants to know what the disc is pays for one pass.
 */
int nb_iso_mount(void);

/* The volume identity as the descriptor holds it, trimmed of the
 * padding spaces, or "" when nothing is mounted. */
const char *nb_iso_volume(void);

/* Files (not directories) in the root directory of the mount. */
unsigned nb_iso_files(void);

/*
 * Find a file by path from the root: "NBFS.IMG" or "DOCS/INNER.TXT".
 * Matching is case-insensitive and ignores the ";1" version suffix
 * every level 1 file carries, because a caller should not have to
 * know how the disc spells a name it already chose.  Returns 1 with
 * *lba (in CD sectors) and *size filled, 0 when there is no such file.
 */
int nb_iso_stat(const char *path, uint32_t *lba, uint32_t *size);

/*
 * Read a byte range from a file's extent: `lba' and `offset' as
 * nb_iso_stat returned them, into dst.  The caller owns the range --
 * this does not know how long the file is, only where it starts --
 * and returns 1 when every sector arrived.
 */
int nb_iso_read(uint32_t lba, unsigned offset, void *dst, unsigned len);

#endif /* NB_ISO9660_H */
