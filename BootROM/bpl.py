from PIL import Image
import numpy as np
import os

img = Image.open("/home/adolf/neobench/splash3.png").convert("P")
assert img.size == (320, 256), "Image must be 320x256"
pixels = np.array(img)
planes = []

for p in range(5):  # 5 bitplanes for 32 colors
    plane = bytearray()
    for y in range(256):
        for xbyte in range(40):  # 320/8
            b = 0
            for bit in range(8):
                px = xbyte * 8 + bit
                if px < 320:
                    v = (pixels[y, px] >> p) & 1
                    b = (b << 1) | v
            plane.append(b)
    planes.append(plane)

os.makedirs("src", exist_ok=True)
with open("src/splash.bpl", "wb") as f:
    for plane in planes:
        f.write(plane)
