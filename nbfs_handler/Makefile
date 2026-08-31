# NBFS read-only dos filesystem handler
#
# host:      native unit test against tests/fixture/fixture.nbfs
# amiga:     m68k-elf build -> handler.elf (to be turned into a hunk via
#            scripts/elf2hunk.py)

SRCDIR  := src
HOSTCC  ?= gcc
M68KCC  ?= m68k-elf-gcc
CFLAGS  ?= -Wall -Wextra -g -O1
M68KCFLAGS := -Wall -Wextra -g -O1 -mcpu=68020 \
              -ffreestanding -fno-builtin -fno-strict-aliasing

CORE_SRCS := $(SRCDIR)/vol.c $(SRCDIR)/nbfs.c
HOST_LIBS := $(SRCDIR)/vol.c $(SRCDIR)/nbfs.c
M68K_LIBS := src/handler.c src/vol.c src/nbfs.c src/mini0.c
SVC_SRC := 68ksvc.S

TEST_BIN := /tmp/opencode/test_nbfs

all: host amiga

.PHONY: host amiga test fixture clean

host: $(TEST_BIN)

$(TEST_BIN): tests/test_nbfs_handler.c $(HOST_LIBS) $(SRCDIR)/dospackets.h \
        $(SRCDIR)/vol.h $(SRCDIR)/fmt.h
	$(HOSTCC) $(CFLAGS) -DNBFH_HOST -I$(SRCDIR) -o $@ \
	    tests/test_nbfs_handler.c $(HOST_LIBS)

fixture: tests/fixture/fixture.nbfs

tests/fixture/fixture.nbfs: tests/make_fixture.py
	python3 tests/make_fixture.py

test: host fixture
	$(TEST_BIN) tests/fixture/fixture.nbfs

amiga: build/nbfs-handler.elf

build/nbfs-handler.elf: crt0.S $(SVC_SRC) $(M68K_LIBS) $(SRCDIR)/dospackets.h \
        $(SRCDIR)/vol.h $(SRCDIR)/fmt.h $(SRCDIR)/api68k.h
	@mkdir -p build
	$(M68KCC) $(M68KCFLAGS) -DNBFH_AMIGA -I$(SRCDIR) -nostdlib \
	    crt0.S $(SVC_SRC) $(M68K_LIBS) -o $@

clean:
	rm -rf build $(TEST_BIN)