#!/usr/bin/env python3
"""
Convert a freestanding ELF32-m68k image into an AmigaDOS Hunk executable.

AmigaDOS will not load an ELF: LoadSeg() wants a HUNK_HEADER followed by
one or more hunks, and it is the loader that fixes up absolute addresses,
so the program may be linked at a nominal address and still run wherever
AllocMem happened to place it.

Everything NeoBench links into a single hunk on purpose.  A HUNK_RELOC32
record is fixed up with the delta of the hunk it *points at*, so code and
the globals it addresses must share one block (and therefore one delta) or
every reference to .bss would be corrected by the wrong amount.  The block
is one contiguous image: .text, .rodata, .data and then .bss as explicit
zero padding, which is why NOBITS sections are emitted as zeros rather
than dropped.

Usage: elf2hunk.py input.elf output.exe
"""
import struct
import sys

HUNK_CODE = 1001
HUNK_RELOC32 = 1004
HUNK_END = 1010
HUNK_HEADER = 1011

SHT_NULL = 0
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHF_ALLOC = 0x2

# m68k relocation kinds.  Absolute 32-bit is the only one that needs the
# loader's help: PC-relative forms are already correct wherever the block
# lands, and GOT forms do not occur in -ffreestanding code.
R_68K_NONE = 0
R_68K_32 = 1
R_68K_16 = 2
R_68K_8 = 3
R_68K_PC32 = 4
R_68K_PC16 = 5
R_68K_PC8 = 6

ELF32_EHDR = struct.Struct(">16sHHIIIIIHHHHHH")
ELF32_SHDR = struct.Struct(">IIIIIIIIII")
ELF32_REL = struct.Struct(">II")
# m68k's GNU ABI records the addend alongside the offset rather than
# folding it into the section, so --emit-relocs yields RELA entries.
ELF32_RELA = struct.Struct(">III")

# Correct wherever the block lands, so the loader can be left out of it.
_NO_FIXUP = {R_68K_NONE, R_68K_PC32, R_68K_PC16, R_68K_PC8}


class Elf:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.raw = fh.read()

        fields = ELF32_EHDR.unpack_from(self.raw, 0)
        (ident, _type, machine, _ver, self.e_entry, _phoff, shoff,
         _flags, _ehsize, _phentsize, _phnum, shentsize, shnum,
         shstrndx) = fields

        # m68k ELF is big-endian, and every struct in this file matches it.
        if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 2:
            sys.exit("elf2hunk: %s is not a big-endian ELF32 file" % path)
        if machine != 4:
            sys.exit("elf2hunk: %s is not m68k (e_machine=%d)" % (path, machine))

        self.shdr = [ELF32_SHDR.unpack_from(self.raw, shoff + i * shentsize)
                     for i in range(shnum)]
        strtab = self.shdr[shstrndx]
        self.shstr = self.raw[strtab[4]:strtab[4] + strtab[5]]

    def name(self, index):
        sh = self.shdr[index]
        start = sh[0]
        return self.shstr[start:self.shstr.index(b"\0", start)].decode("ascii")

    def payload(self, sh):
        return self.raw[sh[4]:sh[4] + sh[5]]


def build(elf):
    """Return (image_bytes, sorted_relocation_offsets, link_base)."""
    alloc = sorted((s for s in elf.shdr
                    if s[1] != SHT_NULL and (s[2] & SHF_ALLOC)),
                   key=lambda s: s[3])       # by sh_addr

    if not alloc:
        sys.exit("elf2hunk: no SHF_ALLOC sections in the image")

    link_base = alloc[0][3]
    link_end = max(s[3] + s[5] for s in alloc)
    span = (link_end - link_base + 3) & ~3
    if span == 0:
        sys.exit("elf2hunk: image is empty")

    # NOBITS contributes address space only; the image starts zeroed,
    # which is exactly what .bss asks for.
    image = bytearray(span)
    for sh in alloc:
        if sh[1] == SHT_NOBITS:
            continue
        off = sh[3] - link_base
        image[off:off + sh[5]] = elf.payload(sh)

    relocs = []
    for sh in elf.shdr:
        if sh[1] not in (SHT_REL, SHT_RELA) or sh[5] == 0:
            continue
        target = sh[7]                       # sh_info: section being fixed up
        if not (elf.shdr[target][2] & SHF_ALLOC):
            continue

        rela = sh[1] == SHT_RELA
        raw = elf.payload(sh)
        entsize = sh[9] or (ELF32_RELA.size if rela else ELF32_REL.size)
        for i in range(0, sh[5], entsize):
            if rela:
                r_offset, r_info, _addend = ELF32_RELA.unpack_from(raw, i)
            else:
                r_offset, r_info = ELF32_REL.unpack_from(raw, i)
            r_type = r_info & 0xFF
            if r_type in _NO_FIXUP:
                continue
            if r_type != R_68K_32:
                sys.exit("elf2hunk: %s uses relocation type %d; only 32-bit "
                         "absolute references can be handed to the loader"
                         % (elf.name(target), r_type))
            pos = r_offset - link_base
            if not 0 <= pos <= span - 4:
                sys.exit("elf2hunk: relocation at 0x%08x falls outside the "
                         "image" % r_offset)
            relocs.append(pos)

    relocs.sort()
    return bytes(image), relocs, link_base


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: elf2hunk.py input.elf output.exe")

    elf = Elf(sys.argv[1])
    image, relocs, link_base = build(elf)

    out = bytearray()

    def long(value):
        out.extend(struct.pack(">I", value & 0xFFFFFFFF))

    longwords = len(image) // 4

    long(HUNK_HEADER)
    long(0)                       # resident library name: none
    long(1)                       # hunk table size
    long(0)                       # first hunk
    long(0)                       # last hunk
    long(longwords)               # hunk 0, length in longwords

    long(HUNK_CODE)
    long(longwords)
    out.extend(image)

    if relocs:
        # One run against hunk 0, then a zero count to end the list.
        long(HUNK_RELOC32)
        long(len(relocs))
        long(0)
        for pos in relocs:
            long(pos)
        long(0)

    long(HUNK_END)

    with open(sys.argv[2], "wb") as fh:
        fh.write(out)

    sys.stderr.write(
        "elf2hunk: base 0x%08x, %d bytes, %d relocation(s) -> %s\n"
        % (link_base, len(image), len(relocs), sys.argv[2]))


if __name__ == "__main__":
    main()
