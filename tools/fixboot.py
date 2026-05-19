import struct

path = "build/bootblock.bin"

data = bytearray(open(path, "rb").read())

# pad to 1024 bytes
data += b"\x00" * (1024 - len(data))

# checksum is sum of 32-bit words = 0
words = struct.iter_unpack(">I", data)
total = sum(w[0] for w in words) & 0xFFFFFFFF

checksum = (-total) & 0xFFFFFFFF

struct.pack_into(">I", data, 4, checksum)

open("build/bootblock_fixed.bin", "wb").write(data)

print("Bootblock fixed")
