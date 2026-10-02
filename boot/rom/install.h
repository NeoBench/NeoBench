#ifndef NB_INSTALL_H
#define NB_INSTALL_H

#include <stdint.h>

/*
 * The installer: NeoBench writing itself to a hard disk from the
 * payload on the install disc.
 *
 * The payload is one file, NBFS.IMG -- a whole NBFS volume built by
 * tools/mknbfs.py from the system tree -- and installing it is
 * therefore a stream rather than a set of file operations: read a
 * sector from the CD, write four to the disk, until the image is on
 * the platter.  The volume arrives in one piece because that is the
 * only way it can be written by a system that owns no filesystem yet
 * to write files into.
 *
 * The policy is deliberately conservative.  The installer runs on its
 * own at boot, with no one to ask, so it only ever writes a disk that
 * is completely blank: NeoBench's own boot block says the disk is
 * already installed, anything else says it belongs to someone else and
 * is left alone.  A first run formats nothing and touches nothing but
 * the sectors of the image itself.
 */

enum nb_install_state
{
    NB_INSTALL_NO_PAYLOAD,  /* the disc is not NeoBench's installer   */
    NB_INSTALL_NO_DISK,     /* nothing writable to install to         */
    NB_INSTALL_REFUSED,     /* the disk holds someone else's data     */
    NB_INSTALL_DAMAGED,     /* our boot block, a superblock that died */
    NB_INSTALL_PRESENT,     /* NeoBench is already on the disk        */
    NB_INSTALL_DONE,        /* written by this boot                   */
    NB_INSTALL_FAILED       /* a transfer refused halfway             */
};

struct nb_install
{
    enum nb_install_state state;
    unsigned sectors;       /* image sectors (512 bytes) on the disk  */
    char     volume[16];    /* NBFS volume name found on the disk     */

    /*
     * Where a failed stream stopped, for the log: "cd" is a disc read
     * that refused, "hd" a disk write, "verify" the read-back of the
     * superblock -- each with the sector it was on.  Zero when the run
     * did not get as far as a transfer.  A failure with no sector
     * named would leave the same question in the log that a silent
     * installer would, so this is part of the answer rather than a
     * debugging leftover.
     */
    const char *op;
    uint32_t    lba;
};

/*
 * Install, or work out why not.  The disc must be mounted already --
 * nb_iso_mount() has run and answered NB_ISO_INSTALL -- and the disk
 * is found through the device table as "ata.device" unit 0, which is
 * the machine's first fixed disk wherever the controller put it.
 * Emits one ">install" serial line whatever happens.
 */
void nb_install_run(struct nb_install *r);

#endif /* NB_INSTALL_H */
