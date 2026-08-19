CC      = cc
CFLAGS  = -Wall -Wextra -std=c11 -O2 \
          -I. \
          -I../../include \
          -I../../include/vfs \
          -I../../../libs/libnbfs/include \
          -I../../../include \
          -I../../../shared/include

LIBNBFS = ../../../libs/libnbfs/build/libnbfs.a

OBJS = \
	dentry.o \
	file.o \
	filesystem.o \
	mount.o \
	nbfs_vfs.o \
	path.o \
	vfs.o \
	vnode.o \
	tests/console_stub.o

TESTS = \
	tests/test_vfs \
	tests/test_vfs_open \
	tests/test_nbfs_lookup \
	tests/test_nbfs_nested_lookup \
	tests/test_nbfs_dentry \
	tests/test_nbfs_file_read \
        tests/test_vfs_seek \
        tests/test_vfs_tell

all: $(TESTS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/test_vfs: tests/test_vfs.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_vfs.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

tests/test_vfs_open: tests/test_vfs_open.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_vfs_open.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

tests/test_nbfs_lookup: tests/test_nbfs_lookup.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_nbfs_lookup.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

tests/test_nbfs_nested_lookup: tests/test_nbfs_nested_lookup.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_nbfs_nested_lookup.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

tests/test_nbfs_dentry: tests/test_nbfs_dentry.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_nbfs_dentry.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

tests/test_nbfs_file_read: tests/test_nbfs_file_read.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_nbfs_file_read.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $@

test: $(TESTS)
	@set -e; \
	for test in $(TESTS); do \
		echo; \
		echo "===== $$test ====="; \
		./$$test; \
	done

clean:
	rm -f *.o
	rm -f $(TESTS)

.PHONY: all test clean

tests/test_vfs_seek: tests/test_vfs_seek.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
			tests/test_vfs_seek.c \
			$(OBJS) \
			$(LIBNBFS) \
			-o $@

tests/test_vfs_tell: tests/test_vfs_tell.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
			tests/test_vfs_tell.c \
			$(OBJS) \
			$(LIBNBFS) \
			-o $@
