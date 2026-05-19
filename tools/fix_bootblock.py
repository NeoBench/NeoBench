import struct

path = "build/bootblock.bin"

with open(path, "rb") as f:
    data = bytearray(f.read())

# pad to exactly 1024 bytes
data += b'\x00' * (1024 - len(data))

# clear checksum field first
struct.pack_into(">I", data, 4, 0)

total = 0

for i in range(0, 1024, 4):
    word = struct.unpack(">I", data[i:i+4])[0]
    total = (total + word) & 0xFFFFFFFF

checksum = (-total) & 0xFFFFFFFF

struct.pack_into(">I", data, 4, checksum)

with open("build/bootblock_fixed.bin", "wb") as f:
    f.write(data)

print("Bootblock fixed")
