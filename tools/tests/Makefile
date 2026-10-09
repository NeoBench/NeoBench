# Host tests: the parts of the ROM that are pure arithmetic and can be
# checked on the machine doing the building rather than in the emulator.
#
#   make -C tools/tests          build and run everything

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99

TESTS := test_zorro test_hid test_pdf test_iso9660 test_scsi test_bfg test_prefs \
         test_store test_gfx

# The Rust half of the ROM, built for this host rather than for the
# m68k: libs/nb_rs parses the four Config files exactly the way
# boot/rom/prefs.c does, and test_prefs holds the two halves together
# over the same bytes -- the same call kernel_init makes at boot.
# cargo and rustc are the ones libs/nb_rs/README pins down, and
# neither sits on a bare PATH by default.
CARGO  := $(HOME)/.cargo/bin/cargo
RUSTC  := $(HOME)/.cargo/bin/rustc
RS_DIR := ../../libs/nb_rs
RS_LIB := $(RS_DIR)/target/release/libnb_rs.a

# The install disc the last test reads is built by the tools that build
# the real one: mknbfs.py packs fixtures/nbfs-tree into a volume, that
# volume is staged as NBFS.IMG beside the ISO tree's files, and mkiso.py
# presses the lot into build/fixture.iso.  Rebuilding it keeps the test
# honest about what a disc actually looks like on the wire.
FIXTURE := build/fixture.iso

all: run

test_zorro: test_zorro.c ../../boot/rom/zorro.c ../../boot/rom/zorro.h \
            ../../boot/rom/zz9000.h
	$(CC) $(CFLAGS) -o $@ test_zorro.c ../../boot/rom/zorro.c

# The HID parser and nothing else: it is the one part of the USB path
# with no hardware behind it anywhere (usb.c), so it is also the one part
# that can be driven from a table of reports here.
test_hid: test_hid.c ../../boot/rom/hid.c ../../boot/rom/hid.h \
          ../../boot/rom/kbd.h
	$(CC) $(CFLAGS) -o $@ test_hid.c ../../boot/rom/hid.c

# The DEFLATE decoder and the PDF reader: both are pure byte shuffling
# with no hardware and no caller to stand in for, and both are driven from
# data zlib and a hand-built document produced -- which is the only way to
# know the reader still reads rather than that it still compiles.
test_pdf: test_pdf.c ../../boot/rom/inflate.c ../../boot/rom/inflate.h \
          ../../boot/rom/pdf.c ../../boot/rom/pdf.h
	$(CC) $(CFLAGS) -o $@ test_pdf.c ../../boot/rom/inflate.c ../../boot/rom/pdf.c

$(FIXTURE): fixtures/iso-tree/INSTALL.TXT fixtures/iso-tree/DOCS/INNER.TXT \
            fixtures/nbfs-tree/hello.txt fixtures/nbfs-tree/sub/inner.txt \
            ../../tools/mkiso.py ../../tools/mknbfs.py
	rm -rf build
	mkdir -p build/iso/DOCS
	python3 ../../tools/mknbfs.py fixtures/nbfs-tree build/iso/NBFS.IMG --blocks 400
	cp fixtures/iso-tree/INSTALL.TXT build/iso/
	cp fixtures/iso-tree/DOCS/INNER.TXT build/iso/DOCS/
	python3 ../../tools/mkiso.py build/iso build/fixture.iso

# The reader, the installer and the device table, with the two fake
# devices the test registers standing in for the hardware (test file
# header has the whole picture).
test_iso9660: test_iso9660.c ../../boot/rom/iso9660.c \
              ../../boot/rom/iso9660.h ../../boot/rom/install.c \
              ../../boot/rom/install.h ../../boot/rom/dev.c \
              ../../boot/rom/dev.h ../../boot/rom/amiga.h $(FIXTURE)
	$(CC) $(CFLAGS) -o $@ test_iso9660.c ../../boot/rom/iso9660.c \
	    ../../boot/rom/install.c ../../boot/rom/dev.c

# The NCR53C710 driver: the chip is only in one motherboard, so the
# register decode, the SCRIPTS encodings and the command descriptor
# layout are held against the values the emulator's lsi53c710 decodes
# them with rather than against a bus.  The driver is included rather
# than linked because everything worth checking in it is static (test
# file header has the picture).
test_scsi: test_scsi.c ../../boot/rom/scsi.c ../../boot/rom/scsi.h \
           ../../boot/rom/amiga.h
	$(CC) $(CFLAGS) -o $@ test_scsi.c

# The BFG9060 signature decoder: the card is in the CPU slot of an
# A3000 or an A4000, so neither the emulator nor the build machine has
# one and the word it leaves is all there is to hold against the code.
# probe.c is included rather than linked because everything worth
# checking in it is static (test file header has the picture).  The
# warning suppression is for probe.c casting 32-bit machine addresses
# to pointers, which is exactly right on the m68k and exactly what a
# 64-bit host should complain about; the read those casts belong to is
# never reached here.
test_bfg: test_bfg.c ../../boot/rom/probe.c ../../boot/rom/probe.h \
          ../../boot/rom/amiga.h
	$(CC) $(CFLAGS) -Wno-int-to-pointer-cast -o $@ test_bfg.c

# Preference parsing, both halves at once: prefs.c and prefs.rs parse
# the same four files -- from the real system/Config ones down to the
# quirks a malformed line falls into -- and the test fails on any
# field where they disagree, then pins the values themselves and ends
# with a mask that has to name the one field it moved.  The archive is
# cargo's host staticlib; it goes on the link line after the C, the
# way the ROM's archive does.
$(RS_LIB): $(RS_DIR)/Cargo.toml $(RS_DIR)/Cargo.lock \
           $(RS_DIR)/src/lib.rs $(RS_DIR)/src/prefs.rs \
           $(RS_DIR)/src/store.rs $(RS_DIR)/src/gfx.rs
	@command -v $(CARGO) >/dev/null 2>&1 || \
	    { echo "rust: cargo is required and was not found at $(CARGO)"; exit 1; }
	cd $(RS_DIR) && RUSTC=$(RUSTC) RUSTFLAGS="-Cpanic=abort" \
	    $(CARGO) build --release

test_prefs: test_prefs.c ../../boot/rom/prefs.c ../../boot/rom/prefs.h \
            ../../boot/rom/pfs.h $(RS_LIB)
	$(CC) $(CFLAGS) -o $@ test_prefs.c ../../boot/rom/prefs.c \
	    $(RS_LIB) -lpthread -ldl -lm

# The store's paths and the program table, held against their Rust
# twins the way test_prefs holds the parser: pfs.c and pfs_data.c are
# the real table the ROM links, progs.c is what main.c used to carry
# on its own (program_slot_of moved out of the desktop so this could
# link it rather than a copy), and the batteries are the ones
# rs_store() builds at boot, with the (directory, after) sweep made
# exhaustive because the host can afford it.  Negative controls
# falsify the C's answers one family at a time.
test_store: test_store.c ../../user/gui/desktop/progs.c \
            ../../user/gui/desktop/progs.h ../../boot/rom/pfs.c \
            ../../boot/rom/pfs.h ../../boot/rom/pfs_data.c $(RS_LIB)
	$(CC) $(CFLAGS) -o $@ test_store.c ../../user/gui/desktop/progs.c \
	    ../../boot/rom/pfs.c ../../boot/rom/pfs_data.c \
	    $(RS_LIB) -lpthread -ldl -lm

# The Vista chrome's paint, held against the C standing over it the
# way test_prefs holds the parser: rs_gfx()'s battery in
# kernel_init carries the same two formulas and the same three
# tables, and this runs them over every Config glass and a set of
# backdrops, then corrupts each family one word at a time and
# requires the mask to be exactly that family's bit with `at` the
# word that moved -- the boot only ever tries one pair, so the host
# sweeps what the boot cannot.
test_gfx: test_gfx.c ../../boot/rom/gfx.h $(RS_LIB)
	$(CC) $(CFLAGS) -o $@ test_gfx.c \
	    $(RS_LIB) -lpthread -ldl -lm

run: $(TESTS) $(FIXTURE)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@cd $(RS_DIR) && RUSTC=$(RUSTC) $(CARGO) test --quiet

clean:
	rm -f $(TESTS)
	rm -rf build

.PHONY: all run clean
