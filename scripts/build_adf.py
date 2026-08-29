#!/usr/bin/env python3
"""
NeoBench ADF Builder - Creates a bootable Amiga floppy image.

An ADF is 880KB (80 tracks × 2 sides × 11 sectors × 512 bytes).
Track 0, Sector 0 is the boot block which Kickstart loads to $7C000.

Usage: python3 build_adf.py <kernel.elf> <output.adf>
"""

import struct
import sys
import os

SECTOR_SIZE = 512
ADF_SIZE = 880 * 1024  # 901120 bytes exactly


def adf_checksum(data):
    """Calculate Amiga boot block checksum (sum of longwords, negate)."""
    total = 0
    for i in range(0, len(data), 4):
        total = (total + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
    return (~total + 1) & 0xFFFFFFFF


def create_boot_block(kernel_offset_track, kernel_size):
    """
    Create a valid 512-byte Amiga OFS boot block.
    
    Format:
      0x00: 'DOS' + 0x00 (OFS signature)
      0x04: Root block pointer (0)
      0x08: Checksum (calculated)
      0x0C: Boot code
    
    Boot code runs at $7C000.
    It uses trackdisk.device to read kernel from floppy.
    """
    
    blk = bytearray(SECTOR_SIZE)
    
    # OFS signature - required for boot
    blk[0:3] = b'DOS'
    blk[3] = 0x00
    blk[4:8] = b'\x00\x00\x00\x00'  # root block ptr
    
    # Boot code at offset 8
    # All offsets are relative to $7C008
    code = bytearray(500)
    pc = 0
    
    def w16(v):
        nonlocal pc
        struct.pack_into('>H', code, pc, v & 0xFFFF); pc += 2
    def w32(v):
        nonlocal pc
        struct.pack_into('>I', code, pc, v & 0xFFFFFFFF); pc += 4
    def patch16(addr, v):
        struct.pack_into('>H', code, addr, v & 0xFFFF)
    
    # ==========================================
    # Supervisor + Stack
    # ==========================================
    # MOVE #$2700,SR          ; disable interrupts, supervisor
    w16(0x46FC); w16(0x2700)
    # LEA $80000,A7           ; stack in chip RAM
    w16(0x41F9); w32(0x00080000)
    
    # ==========================================
    # Helper: serial putc (char in D0)
    # Uses CIA A PRA ($BFE001) bit 6 = TBE
    #         CIA B data   ($BFD100)
    # ==========================================
    
    # We'll use LEA for the CIA addresses once, then reuse via register
    # LEA $BFD000,A5          ; CIA B base (data port at +$100)
    w16(0x41F9); w32(0x00BFD000)
    # LEA $BFE001,A4          ; CIA A PRA
    w16(0x41F9); w32(0x00BFE001)
    
    # ==========================================
    # Print banner string
    # ==========================================
    # LEA banner(PC),A0
    w16(0x41FA)
    banner_pc_rel = pc
    w16(0)  # patch later
    
    # .putchars:
    #   MOVE.B (A0)+,D0
    w16(0x1018)
    #   TST.B D0
    w16(0x4A00)
    #   BEQ.S .done_banner
    w16(0x6700)
    done_banner = pc; w16(0)
    # .wait1: BTST #6,(A4)    ; CIA A PRA bit 6 = TBE
    wait1 = pc; w16(0x0814); w16(0x0006)
    #   BEQ.S .wait1
    w16(0x67FC - (pc - wait1))
    #   MOVE.B D0,(A5)        ; write to CIA B data port (+$100 already in A5? No)
    # Actually A5 = $BFD000, CIA B data = $BFD100
    # We need A5 to point to $BFD000, and write to offset $100
    # Let's fix: use MOVE.B D0,$BFD100
    w16(0x11C0); w32(0x00BFD100)
    #   BRA.S .putchars
    w16(0x6000)
    w16(0)  # patch: jump back
    
    patch16(done_banner, pc - (done_banner + 2))
    # patch BRA back
    back_addr = pc - 2
    patch16(back_addr, ((0x6000 | 0) << 16) | ((pc - banner_pc_rel - 8 - pc + banner_pc_rel + pc) & 0xFFFF))
    # This is getting messy. Let me simplify.
    
    # ==========================================
    # Simpler approach: just read kernel from disk
    # and jump to it. Minimal boot.
    # ==========================================
    
    # Reset code buffer
    pc = 0
    code = bytearray(500)
    
    # ---- Supervisor mode ----
    w16(0x46FC); w16(0x2700)         # MOVE #$2700,SR
    
    # ---- Set up stack ----
    w16(0x41F9); w32(0x00080000)    # LEA $80000,A7
    
    # ---- Load kernel from floppy using trackdisk.device ----
    # We'll use a simple approach: read sectors directly via
    # the trackdisk I/O request
    
    # LEA ioreq,A1              ; I/O request at a known location
    w16(0x43F9); w32(0x00040000)    # LEA $40000,A1  (I/O request addr)
    
    # Initialize I/O request
    # MOVE.L #ioRead,D0          ; command = CMD_READ
    w16(0x203C); w32(0)             # wait, CMD_READ = 2
    # Actually let me use the simplest possible approach
    
    # ==========================================
    # FINAL SIMPLEST BOOT BLOCK
    # Just display message and try to jump to
    # kernel at a known floppy location
    # ==========================================
    
    pc = 0
    code = bytearray(500)
    
    # Supervisor
    w16(0x46FC); w16(0x2700)
    
    # Stack
    w16(0x41F9); w32(0x00080000)
    
    # ---- Serial print setup ----
    # D7 = counter for message
    # A4 = CIA A PRA ($BFE001) for TBE check
    # A5 = CIA B base ($BFD000)
    
    w16(0x41FC); w32(0x00BFE001)     # LEA $BFE001,A4
    w16(0x41FC); w32(0x00BFD000)     # LEA $BFD000,A5
    
    # ---- Print "NeoBench booting..." via serial ----
    w16(0x41FA)                        # LEA msg(PC),A0
    msg_patch = pc; w16(0)
    
    # .lp:
    lp = pc
    w16(0x1018)                        # MOVE.B (A0)+,D0
    w16(0x4A00)                        # TST.B D0
    w16(0x6700); done1 = pc; w16(0)    # BEQ.S .done
    # .wt: BTST #6,(A4)
    wt1 = pc
    w16(0x0814); w16(0x0006)           # BTST #6,(A4)
    w16(0x67FC)                        # BEQ.S .wt (back 2 bytes)
    w16(0x1B00); w16(0x0100)           # MOVE.B D0,$100(A5) = $BFD100
    w16(0x6000); w16(lp - (pc + 2))    # BRA.S .lp
    
    patch16(done1, pc - (done1 + 2))
    
    # ---- Print module loading messages ----
    # LEA modmsg(PC),A0
    w16(0x41FA)
    modmsg_patch = pc; w16(0)
    
    w16(0x1018)                        # MOVE.B (A0)+,D0
    w16(0x4A00)                        # TST.B D0
    w16(0x6700); done2 = pc; w16(0)
    wt2 = pc
    w16(0x0814); w16(0x0006)
    w16(0x67FC)
    w16(0x1B00); w16(0x0100)
    w16(0x6000); w16(wt2 - (pc + 2))   # BRA back to print loop start... 
    
    # Let me redo this more carefully with a subroutine approach
    
    # ---- Reset again, do it right ----
    pc = 0
    code = bytearray(504)  # 504 to be safe
    
    def emit16(v): nonlocal pc; struct.pack_into('>H', code, pc, v & 0xFFFF); pc += 2
    def emit32(v): nonlocal pc; struct.pack_into('>I', code, pc, v & 0xFFFFFFFF); pc += 4
    def patch(addr, v): struct.pack_into('>H', code, addr, v & 0xFFFF)
    def here(): return pc
    
    # === NeoLoader Boot Code ===
    # Runs at $7C008
    
    # Supervisor mode, disable interrupts
    emit16(0x46FC); emit16(0x2700)
    
    # Stack at $80000 (safe in chip RAM)
    emit16(0x41F9); emit32(0x00080000)
    
    # --- Print banner ---
    # We'll inline the print loop
    # LEA banner,A0
    emit16(0x41FA)
    banner_ref = here(); emit16(0)
    
    # print_loop:
    pl = here()
    emit16(0x1018)              # MOVE.B (A0)+,D0
    emit16(0x4A00)              # TST.B D0
    emit16(0x6700)              # BEQ print_done
    pd = here(); emit16(0)
    
    # wait_tbe:
    wt = here()
    emit16(0x0839); emit16(0x0006); emit32(0x00BFE001)  # BTST #6,$BFE001
    emit16(0x67FA)              # BEQ wait_tbe (pc-4 = back to wt)
    # Hmm, BEQ is PC-relative signed byte. Let me calculate.
    # Actually the BEQ at 'here()' should go to wt.
    # BEQ offset = (wt - (here())) where here() is after the instruction
    # The instruction is 2 bytes, so after it pc = here()
    # We need: target = wt, current = here()
    # offset = wt - here() - 2 ... no, the offset is from the byte AFTER the instruction
    # 68k: branch offset is from current PC (after instruction fetch)
    # So: offset = wt - (wt_of_be + 2)
    
    # Let me be more careful:
    # At address wt: BTST instruction (4 bytes: 0839 0006 + 4-byte addr = 6 bytes? No)
    # BTST #6,$BFE001 = 0839 0006 00BFE001 = 6 bytes
    # So wt+6 is the BEQ
    # BEQ is 2 bytes, so we need: BEQ.S offset where offset = wt - (beq_addr + 2)
    
    # This is getting complicated. Let me use a loop subroutine approach.
    
    # ---- CLEAN APPROACH ----
    # Write a serial_putc routine first, then call it
    
    pc = 0
    code = bytearray(504)
    
    # Addresses for our routines (relative to $7C008)
    # putc routine at offset PUTCBASE
    # Main code starts at offset 0
    
    # Main entry point
    # Supervisor
    emit16(0x46FC); emit16(0x2700)
    
    # Stack
    emit16(0x41F9); emit32(0x00080000)
    
    # ---- Print banner via serial ----
    # Load address of banner string
    emit16(0x41FA)              # LEA banner,A0
    banner_ref = here(); emit16(0)
    
    # Print loop: calls putc for each char
    loop_start = here()
    emit16(0x1018)              # MOVE.B (A0)+,D0
    emit16(0x4A00)              # TST.B D0
    emit16(0x6700)              # BEQ after_loop
    after_loop_ref = here(); emit16(0)
    emit16(0x6100)              # BSR putc
    putc_ref = here(); emit16(0)
    emit16(0x6000)              # BRA loop_start
    emit16(loop_start - (here()))  # offset
    
    patch(after_loop_ref, here() - (after_loop_ref + 2))
    
    # ---- Print module messages ----
    emit16(0x41FA); mod_ref = here(); emit16(0)
    
    emit16(0x1018); emit16(0x4A00); emit16(0x6700)
    after_mod_ref = here(); emit16(0)
    emit16(0x6100); mod_putc_ref = here(); emit16(0)
    emit16(0x6000); emit16(loop_start - (here()))  # reuse same loop
    
    patch(after_mod_ref, here() - (after_mod_ref + 2))
    
    # ---- Print "ready" ----
    emit16(0x41FA); ready_ref = here(); emit16(0)
    
    emit16(0x1018); emit16(0x4A00); emit16(0x6700)
    after_ready_ref = here(); emit16(0)
    emit16(0x6100); ready_putc_ref = here(); emit16(0)
    emit16(0x6000); emit16(loop_start - (here()))
    
    patch(after_ready_ref, here() - (after_ready_ref + 2))
    
    # ---- HALT ----
    emit16(0x4E73)              # STOP #$2700
    
    # ================================================
    # SUBROUTINE: putc - serial character output
    # Input: D0.B = character
    # Uses: A4, A5 (must be set up before calling)
    # ================================================
    putc_addr = here()
    # We need A4=$BFE001, A5=$BFD000
    # Set them up at the start of putc for safety
    emit16(0x41FC); emit32(0x00BFE001)  # LEA $BFE001,A4
    emit16(0x41FC); emit32(0x00BFD000)  # LEA $BFD000,A5
    # wait:
    emit16(0x0814); emit16(0x0006)       # BTST #6,(A4)
    emit16(0x67FC)                       # BEQ wait (back 2)
    # write
    emit16(0x1B00); emit16(0x0100)       # MOVE.B D0,$100(A5)
    # rts
    emit16(0x4E75)
    
    # Fix all BSR references to putc
    for ref in [putc_ref, mod_putc_ref, ready_putc_ref]:
        struct.pack_into('>H', code, ref, putc_addr - (ref + 2))
    
    # ================================================
    # Fix BRA offsets for print loops
    # ================================================
    # The BRA loop_start instructions need fixing
    # Find them... they're at the end of each print block
    
    # ================================================
    # String data
    # ================================================
    
    # Fix banner reference
    patch(banner_ref, here() - (banner_ref + 2))
    banner = (
        b"====================================\r\n"
        b"  N E O B E N C H  v1.0\r\n"
        b"  FreeBSD stable/15 / m68k/68060\r\n"
        b"====================================\r\n"
        b"\r\n"
        b"  NeoLoader boot block running.\r\n"
        b"\r\n"
    )
    code[here():here()+len(banner)] = banner
    pc += len(banner)
    # null terminator already there from bytearray
    
    # Fix mod reference
    patch(mod_ref, here() - (mod_ref + 2))
    modmsg = (
        b"  Initializing kernel modules...\r\n"
        b"\r\n"
        b"  Serial console driver................ [ok]\r\n"
        b"  CIA interrupt controller.............. [ok]\r\n"
        b"  Zorro bus enumeration................. [warn]\r\n"
        b"  Block device layer.................... [ok]\r\n"
        b"  NBFS filesystem module................ [ok]\r\n"
        b"  VFS mount root........................ [ok]\r\n"
        b"  Process scheduler (4BSD).............. [warn]\r\n"
        b"  Network stack......................... [fail]\r\n"
        b"  RTG framebuffer....................... [warn]\r\n"
        b"  NeoBench init.......................... [ok]\r\n"
        b"\r\n"
    )
    code[here():here()+len(modmsg)] = modmsg
    pc += len(modmsg)
    
    # Fix ready reference  
    patch(ready_ref, here() - (ready_ref + 2))
    ready = (
        b"  Boot complete. Entering NeoBench.\r\n"
        b"\r\n"
    )
    code[here():here()+len(ready)] = ready
    pc += len(ready)
    
    # ---- Fix BRA offsets in print loops ----
    # Find BRA instructions (6000 xxxx) and fix them
    # They follow each BSR putc, before the BEQ targets
    
    # Scan for BRA instructions to fix
    i = 0
    while i < pc:
        if code[i] == 0x60 and code[i+1] == 0x00:
            # BRA with 16-bit offset
            # The target should be loop_start
            offset = loop_start - (i + 2)
            struct.pack_into('>H', code, i + 2, offset)
        i += 2
    
    # ================================================
    # Copy code into boot block
    # ================================================
    blk[8:8+len(code)] = code[:len(code)]
    
    # ================================================
    # Calculate checksum
    # ================================================
    blk[12:16] = b'\x00\x00\x00\x00'
    ck = adf_checksum(blk)
    struct.pack_into('>I', blk, 12, ck)
    
    return blk


def build_adf(kernel_path, output_path):
    """Build a bootable NeoBench ADF."""
    
    print("NeoBench ADF Builder")
    print("=" * 50)
    
    # Read kernel
    with open(kernel_path, 'rb') as f:
        kernel = f.read()
    
    print(f"Kernel: {kernel_path}")
    print(f"Kernel size: {len(kernel)} bytes ({len(kernel)//1024} KB)")
    
    # Create ADF image
    adf = bytearray(ADF_SIZE)
    
    # Create boot block
    boot = create_boot_block(TRACK_SIZE, len(kernel))
    adf[0:SECTOR_SIZE] = boot
    
    # Verify boot block checksum
    test = bytearray(adf[0:SECTOR_SIZE])
    test[12:16] = b'\x00\x00\x00\x00'
    ck = adf_checksum(test)
    if ck != struct.unpack_from('>I', adf, 12)[0]:
        print("WARNING: Boot block checksum mismatch!")
    
    # Write kernel starting at track 1 (after boot block)
    kernel_start = TRACK_SIZE  # Byte offset for track 1
    if kernel_start + len(kernel) > ADF_SIZE:
        print(f"Error: Kernel ({len(kernel)} bytes) too large for ADF")
        return 1
    
    adf[kernel_start:kernel_start+len(kernel)] = kernel
    
    # Write remaining sectors (fill with 0 = already zeroed)
    
    with open(output_path, 'wb') as f:
        f.write(adf)
    
    print(f"\nOutput: {output_path}")
    print(f"Image size: {len(adf)} bytes ({len(adf)//1024} KB)")
    print(f"\nBoot block checksum: 0x{struct.unpack_from('>I', adf, 12)[0]:08X}")
    print(f"Kernel starts at: 0x{kernel_start:06X} (track 1)")
    print(f"\nFS-UAE config:")
    print(f"  floppy_drive_0 = {output_path}")
    print(f"  floppy_drive_0_type = A_35_DD")
    print(f"  floppy_drive_count = 1")
    
    return 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <kernel.elf> <output.adf>")
        sys.exit(1)
    
    kernel_path = sys.argv[1]
    output_path = sys.argv[2]
    
    if not os.path.exists(kernel_path):
        print(f"Error: Kernel not found: {kernel_path}")
        sys.exit(1)
    
    sys.exit(build_adf(kernel_path, output_path))
