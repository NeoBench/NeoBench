CC      = cc
CFLAGS  = -Wall -Wextra -std=c11 -O2 \
          -I. \
          -I../../include \
          -I../../include/vfs \
          -I../../../libs/libnbfs/include \
          -I../../../include \
          -I../../../shared/include

LIBNBFS = ../../../libs/libnbfs/build/libnbfs.a

# Host test objects live in build/ so they never collide with the
# m68k object files the kernel build produces in this same directory
# (see kernel/Makefile: fs/vfs/*.o).  Test binaries go to tests/bin/.
BUILD     = build
BINDIR    = tests/bin

VFS_SRCS = \
	dentry.c \
	file.c \
	filesystem.c \
	mount.c \
	nbfs_vfs.c \
	path.c \
	vfs.c \
	vnode.c

STUB_SRCS = \
	tests/console_stub.c \
	tests/kernel_nbfs_stub.c

VFS_OBJS = $(addprefix $(BUILD)/, $(VFS_SRCS:.c=.o))
STUB_OBJS = $(addprefix $(BUILD)/, $(subst tests/,,$(STUB_SRCS:.c=.o)))
OBJS = $(VFS_OBJS) $(STUB_OBJS)

TESTS = \
	test_vfs \
	test_vfs_open \
	test_nbfs_lookup \
	test_nbfs_nested_lookup \
	test_nbfs_dentry \
	test_nbfs_file_read \
	test_vfs_seek \
	test_vfs_tell

TESTS_BIN = $(addprefix $(BINDIR)/, $(TESTS))

all: $(TESTS_BIN)

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: tests/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/test_vfs: tests/test_vfs.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_vfs.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_vfs_open: tests/test_vfs_open.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_vfs_open.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_nbfs_lookup: tests/test_nbfs_lookup.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_nbfs_lookup.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_nbfs_nested_lookup: tests/test_nbfs_nested_lookup.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_nbfs_nested_lookup.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_nbfs_dentry: tests/test_nbfs_dentry.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_nbfs_dentry.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_nbfs_file_read: tests/test_nbfs_file_read.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_nbfs_file_read.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_vfs_seek: tests/test_vfs_seek.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_vfs_seek.c $(OBJS) $(LIBNBFS) -o $@

$(BINDIR)/test_vfs_tell: tests/test_vfs_tell.c $(OBJS) $(LIBNBFS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) tests/test_vfs_tell.c $(OBJS) $(LIBNBFS) -o $@

test: $(TESTS_BIN)
	@set -e; \
	for t in $(TESTS); do \
		echo; \
		echo "===== $$t ====="; \
		./$(BINDIR)/$$t; \
	done

clean:
	rm -rf $(BUILD)
	rm -rf $(BINDIR)
	rm -f tests/console_stub.o tests/kernel_nbfs_stub.o

.PHONY: all test clean
