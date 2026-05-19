import struct

fn = "build/bootblock.bin"

with open(fn, "rb") as f:
    data = bytearray(f.read())

struct.pack_into(">I", data, 4, 0)

s = 0

for i in range(0, 1024, 4):
    w = struct.unpack(">I", data[i:i+4])[0]
    s = (s + w) & 0xffffffff

ck = (-s) & 0xffffffff

struct.pack_into(">I", data, 4, ck)

with open("build/bootblock_fixed.bin", "wb") as f:
    f.write(data)

print("checksum:", hex(ck))
