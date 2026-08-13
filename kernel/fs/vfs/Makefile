CC      = cc
CFLAGS  = -Wall -Wextra -std=c11 -O2 \
          -Iinclude \
          -I../../../libs/libnbfs/include \
          -I../../../include \
          -I../../../shared/include

LIBNBFS = ../../../libs/libnbfs/build/libnbfs.a

OBJS = dentry.o file.o filesystem.o mount.o path.o vfs.o vnode.o

TEST = tests/test_vfs

all: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST): tests/test_vfs.c $(OBJS) $(LIBNBFS)
	$(CC) $(CFLAGS) \
		tests/test_vfs.c \
		$(OBJS) \
		$(LIBNBFS) \
		-o $(TEST)

test: $(TEST)
	./$(TEST)

clean:
	rm -f *.o
	rm -f $(TEST)
	rm -f tests/test_nbfs_lookup

.PHONY: all test clean
