# Host tests: the parts of the ROM that are pure arithmetic and can be
# checked on the machine doing the building rather than in the emulator.
#
#   make -C tools/tests          build and run everything

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TESTS := test_zorro

all: run

test_zorro: test_zorro.c ../../boot/rom/zorro.c ../../boot/rom/zorro.h \
            ../../boot/rom/zz9000.h
	$(CC) $(CFLAGS) -o $@ test_zorro.c ../../boot/rom/zorro.c

run: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
