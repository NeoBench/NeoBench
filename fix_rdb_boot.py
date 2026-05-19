import struct
import sys

def calculate_checksum(block):
    # Standard Amiga RDB checksum: sum of all longs must be 0
    data = bytearray(block)
    struct.pack_into(">I", data, 8, 0) # Clear existing checksum
    chk = 0
    for i in range(0, 512, 4):
        chk += struct.unpack(">I", data[i:i+4])[0]
    chk_val = (0 - (chk & 0xFFFFFFFF)) & 0xFFFFFFFF
    struct.pack_into(">I", data, 8, chk_val)
    return data

with open(sys.argv[1], "r+b") as f:
    # Read PART block for DH0 (it's at block 1 = 512 bytes)
    f.seek(512)
    part = bytearray(f.read(512))
    
    if part[:4] != b"PART":
        print("PART block not found at sector 1")
        sys.exit(1)

    # Update BootPri to 127 (Max priority)
    # Environment starts at 128. BootPri is at index 15 (60 bytes).
    # 128 + 60 = 188
    struct.pack_into(">I", part, 188, 127)
    
    # Update DosType to "NEO\0" (0x4E454F00)
    # 128 + 64 = 192
    struct.pack_into(">I", part, 192, 0x4E454F00)
    
    # Re-calculate checksum
    part = calculate_checksum(part)
    
    f.seek(512)
    f.write(part)
    print("DH0 Partition updated: BootPri=127, DosType=NEO\0")

