# NeoBench NBFS Kernel Integration Plan

## Current status

The NeoBench filesystem stack has reached the point where userspace NBFS and the VFS test suite are passing:

- NBFS image opening
- VFS filesystem initialization
- root vnode initialization
- `/docs` lookup
- `/docs/readme.txt` nested lookup
- regular-file type validation
- file contents read
- sequential EOF behavior
- deep NBFS lookup

The kernel already contains:

- block-device abstraction
- NBFS kernel initialization and mount structures
- VFS filesystem registration
- VFS mount/vnode structures
- kernel startup calls to `nbfs_kernel_init()` and `vfs_init()`

The remaining work is to replace the kernel NBFS stubs with real on-disk access and then make NBFS the root filesystem.

## Integration sequence

```text
m68k kernel boot
        |
        v
block device layer
        |
        v
kernel NBFS superblock reader
        |
        v
NBFS inode + directory lookup
        |
        v
NBFS -> VFS vnode adapter
        |
        v
VFS filesystem registration
        |
        v
root mount "/"
        |
        v
NBFS ROOTFS
```

## Next step: memory-backed block device

Do not change `kernel_main.c` yet.

First create a kernel test block device backed by an NBFS image in memory. This isolates filesystem integration from physical Amiga storage hardware.

Target flow:

```text
NBFS image
    |
    v
memory block device
    |
    v
block_device_read()
    |
    v
nbfs_kernel_mount()
    |
    v
NBFS superblock validation
    |
    v
root inode discovered
```

## Kernel NBFS superblock reader

Replace the current placeholder values in `kernel/fs/nbfs/nbfs.c`:

```c
nbfs_mount.block_size = fs->block_size;
nbfs_mount.block_count = 0;
nbfs_mount.root_inode = 1;
```

with actual on-disk superblock parsing.

The mount operation should:

1. Read the NBFS superblock block through `block_device_read()`.
2. Validate the `NBFS` magic.
3. Validate the filesystem version.
4. Validate the block size.
5. Read the actual block count.
6. Read the actual root inode.
7. Store the validated values in `nbfs_mount_t`.
8. Mark the filesystem mounted.

The kernel must not link against `libnbfs.a`.

## Kernel directory lookup

The current `nbfs_kernel_lookup()` is a deliberate stub and returns `-1`.

It needs to implement the same on-disk lookup behavior already demonstrated by the userspace NBFS tests:

```text
/
+-- docs
    +-- readme.txt
    +-- subdir
        +-- file.txt
```

The kernel lookup path should operate directly through the block-device interface and NBFS on-disk structures.

## VFS integration

The existing VFS filesystem structure already provides:

```c
vfs_lookup_fn lookup;
```

The kernel NBFS mount already assigns:

```c
fs->private_data = &nbfs_mount;
fs->lookup = nbfs_kernel_lookup;
```

The final kernel path should therefore be:

```text
nbfs_kernel_lookup()
        |
        v
VFS vnode
        |
        v
vfs_lookup()
        |
        v
vfs_path
        |
        v
vfs_file_read()
```

The userspace `kernel/fs/vfs/nbfs_vfs.c` adapter used by tests can remain separate from the eventual freestanding kernel implementation.

## Kernel root mount

Once superblock parsing, lookup, and file reading work in the kernel, update startup so the root filesystem is mounted from NBFS.

Expected boot progression:

```text
NeoBench kernel
----------------
BLOCK:     initializing... OK
NBFS:      initializing... OK
VFS:       initializing... OK
NBFS:      probing root device... OK
NBFS:      reading superblock... OK
NBFS:      mounting root filesystem... OK
VFS:       mounting "/"... OK

NeoBench kernel ready.
```

## Makefile integration

The kernel Makefile currently includes core VFS objects but does not yet include every VFS component needed for the final filesystem path. Eventually review/add:

- `fs/vfs/dentry.o`
- `fs/vfs/mount.o`
- kernel-compatible NBFS/VFS adapter objects as appropriate

Avoid linking the userspace `libnbfs.a` into the kernel.

## Final objective

Once the above stages pass, NBFS becomes NeoBench's actual `/` filesystem:

```text
NBFS block device
      |
      v
    NBFS
      |
      v
     VFS
      |
      v
     root /
      |
      +-- system
      +-- bin
      +-- dev
      +-- etc
      +-- home
      +-- lib
      +-- ...
```

## Rule for implementation order

Do not debug boot, storage hardware, filesystem parsing, VFS, and pathname resolution simultaneously. Prove each layer independently, then connect the layers in order.
