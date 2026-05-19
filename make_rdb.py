import struct

def calculate_checksum(block):
    chk = 0
    for i in range(0, 512, 4):
        chk += struct.unpack(">I", block[i:i+4])[0]
    # Sum of all longs must be 0
    return (0 - (chk & 0xFFFFFFFF)) & 0xFFFFFFFF

# 32MB HDF = 65536 sectors of 512 bytes
# Heads=16, SectorsPerTrack=63 -> CylSize=1008
# Total Cyls = 65536 / 1008 = 65.01 -> 65 Cyls

# RDSK Block (Sector 0)
rdsk = bytearray(512)
struct.pack_into(">4s", rdsk, 0, b"RDSK")
struct.pack_into(">I", rdsk, 4, 128) # Size in longs
struct.pack_into(">I", rdsk, 12, 7)   # HostID
struct.pack_into(">I", rdsk, 16, 512) # BlockSize
struct.pack_into(">I", rdsk, 20, 2)   # Flags (LastRDB)
struct.pack_into(">I", rdsk, 32, 65)  # Cylinders
struct.pack_into(">I", rdsk, 36, 63)  # Sectors
struct.pack_into(">I", rdsk, 40, 16)  # Heads
struct.pack_into(">I", rdsk, 124, 1)  # First Partition Block (Sector 1)
struct.pack_into(">I", rdsk, 8, calculate_checksum(rdsk))

# PART Block (Sector 1)
part = bytearray(512)
struct.pack_into(">4s", part, 0, b"PART")
struct.pack_into(">I", part, 4, 128) # Size in longs
struct.pack_into(">I", part, 16, 0xFFFFFFFF) # Next partition (None)
struct.pack_into(">I", part, 20, 1)   # Flags (Bootable)
part[36] = 3
part[37:40] = b"DH0"
# Environment Table (starts at offset 64)
struct.pack_into(">I", part, 64, 16)   # TableSize
struct.pack_into(">I", part, 68, 128)  # SizeBlock (longs)
struct.pack_into(">I", part, 76, 16)   # Surfaces (Heads)
struct.pack_into(">I", part, 80, 1)    # SectorsPerBlock
struct.pack_into(">I", part, 84, 63)   # BlocksPerTrack
struct.pack_into(">I", part, 100, 1)   # LowCyl
struct.pack_into(">I", part, 104, 64)  # HighCyl
struct.pack_into(">I", part, 108, 5)   # PBuffers
struct.pack_into(">I", part, 124, 10)  # BootPri
struct.pack_into(">I", part, 128, 0x444F5300) # DosType (DOS\0)
struct.pack_into(">I", part, 8, calculate_checksum(part))

with open("build/rdb.bin", "wb") as f:
    f.write(rdsk)
    f.write(part)
    # Total 16 sectors reserved for RDB scan
    f.write(b"\x00" * (512 * 14))

print("RDB Binary generated.")
