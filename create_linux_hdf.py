import struct
import os

def calculate_checksum(block):
    chk = 0
    for i in range(0, 512, 4):
        chk += struct.unpack(">I", block[i:i+4])[0]
    return (0 - (chk & 0xFFFFFFFF)) & 0xFFFFFFFF

def create_hdf(output_path, boot_size_mb, root_img_path):
    heads = 16
    sectors = 63
    track_size = heads * sectors # 1008
    
    # 256MB total
    total_cyls = 520
    
    # Partition 1: BOOT (DH0)
    boot_cyls = 32 # ~32MB
    # Partition 2: ROOT (DH1)
    root_cyls = 200 # ~200MB
    
    # RDSK Block
    rdsk = bytearray(512)
    struct.pack_into(">4s", rdsk, 0, b"RDSK")
    struct.pack_into(">I", rdsk, 4, 128)
    struct.pack_into(">I", rdsk, 12, 7)
    struct.pack_into(">I", rdsk, 16, 512)
    struct.pack_into(">I", rdsk, 20, 2) # LastRDB
    struct.pack_into(">I", rdsk, 32, total_cyls)
    struct.pack_into(">I", rdsk, 36, sectors)
    struct.pack_into(">I", rdsk, 40, heads)
    struct.pack_into(">I", rdsk, 124, 1) # First Partition at sector 1
    struct.pack_into(">I", rdsk, 8, calculate_checksum(rdsk))
    
    # PART Block 1 (DH0)
    part1 = bytearray(512)
    struct.pack_into(">4s", part1, 0, b"PART")
    struct.pack_into(">I", part1, 4, 128)
    struct.pack_into(">I", part1, 16, 2) # Next partition at sector 2
    struct.pack_into(">I", part1, 20, 1) # Bootable
    part1[36] = 3
    part1[37:40] = b"DH0"
    struct.pack_into(">I", part1, 64, 16) # TableSize
    struct.pack_into(">I", part1, 68, 128)
    struct.pack_into(">I", part1, 76, heads)
    struct.pack_into(">I", part1, 80, 1)
    struct.pack_into(">I", part1, 84, sectors)
    struct.pack_into(">I", part1, 100, 1) # LowCyl
    struct.pack_into(">I", part1, 104, 1 + boot_cyls) # HighCyl
    struct.pack_into(">I", part1, 124, 10) # BootPri
    struct.pack_into(">I", part1, 128, 0x444F5303) # DOS\3 (FFS)
    struct.pack_into(">I", part1, 8, calculate_checksum(part1))
    
    # PART Block 2 (DH1)
    part2 = bytearray(512)
    struct.pack_into(">4s", part2, 0, b"PART")
    struct.pack_into(">I", part2, 4, 128)
    struct.pack_into(">I", part2, 16, 0xFFFFFFFF) # End of chain
    struct.pack_into(">I", part2, 20, 0)
    part2[36] = 3
    part2[37:40] = b"DH1"
    struct.pack_into(">I", part2, 64, 16)
    struct.pack_into(">I", part2, 68, 128)
    struct.pack_into(">I", part2, 76, heads)
    struct.pack_into(">I", part2, 80, 1)
    struct.pack_into(">I", part2, 84, sectors)
    struct.pack_into(">I", part2, 100, 1 + boot_cyls + 1) # LowCyl
    struct.pack_into(">I", part2, 104, 1 + boot_cyls + 1 + root_cyls) # HighCyl
    struct.pack_into(">I", part2, 128, 0x4C4E5800) # LNX\0
    struct.pack_into(">I", part2, 8, calculate_checksum(part2))
    
    with open(output_path, "wb") as f:
        f.write(rdsk)
        f.write(part1)
        f.write(part2)
        f.write(b"\x00" * (512 * 13)) # Padding to 16 sectors
        
        # Seek to start of DH1 and write miniroot
        root_start_sector = (1 + boot_cyls + 1) * track_size
        f.seek(root_start_sector * 512)
        with open(root_img_path, "rb") as root_f:
            f.write(root_f.read())
            
        # Final padding to full size
        f.seek(total_cyls * track_size * 512 - 1)
        f.write(b"\x00")

if __name__ == "__main__":
    create_hdf("linux_m68k.hdf", 32, "/home/lordp/Downloads/miniroot.fs")
    print("linux_m68k.hdf created successfully.")
