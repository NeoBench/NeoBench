import struct
import sys

def get_checksum(block):
    chk = 0
    for i in range(0, 512, 4):
        chk += struct.unpack(">I", block[i:i+4])[0]
    return chk & 0xFFFFFFFF

with open(sys.argv[1], "rb") as f:
    rdsk = f.read(512)
    if rdsk[:4] != b"RDSK":
        print("No RDSK found at sector 0")
        sys.exit(1)
    
    print("RDSK Found")
    next_part = struct.unpack(">I", rdsk[124:128])[0]
    
    while next_part != 0xFFFFFFFF:
        f.seek(next_part * 512)
        part = f.read(512)
        if part[:4] != b"PART":
            print(f"Broken partition chain at sector {next_part}")
            break
        
        name_len = part[36]
        name = part[37:37+name_len].decode('ascii', 'ignore')
        low_cyl = struct.unpack(">I", part[100:104])[0]
        high_cyl = struct.unpack(">I", part[104:108])[0]
        surfaces = struct.unpack(">I", part[76:80])[0]
        blocks_per_track = struct.unpack(">I", part[84:88])[0]
        
        start_sector = low_cyl * surfaces * blocks_per_track
        print(f"Partition: {name} | Sector Start: {start_sector} | LowCyl: {low_cyl} | HighCyl: {high_cyl}")
        
        next_part = struct.unpack(">I", part[16:20])[0]

