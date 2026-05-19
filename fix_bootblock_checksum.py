import struct
import sys

with open(sys.argv[1], "r+b") as f:
    data = f.read(1024)
    if len(data) < 1024:
        data = data.ljust(1024, b"\x00")
    
    # Reset checksum field
    data = data[:4] + b"\x00\x00\x00\x00" + data[8:]
    
    checksum = 0
    for i in range(0, 1024, 4):
        val = struct.unpack(">I", data[i:i+4])[0]
        checksum += val
        if checksum > 0xFFFFFFFF:
            checksum = (checksum + 1) & 0xFFFFFFFF
            
    checksum = (~checksum) & 0xFFFFFFFF
    
    f.seek(4)
    f.write(struct.pack(">I", checksum))
