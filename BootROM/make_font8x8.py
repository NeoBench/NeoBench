# make_font8x8.py
font = []
for c in range(256):
    if 32 <= c < 127:
        for y in range(8):
            font.append(0xFF if y in [2,5] else 0x00)
    else:
        font.extend([0x00]*8)

with open("font8x8.raw", "wb") as f:
    f.write(bytes(font))
print("font8x8.raw written, 2048 bytes")
