from PIL import Image

img = Image.open("splash.png").convert("P")
palette = img.getpalette()[:32*3]  # 32 colors, RGB each

with open("splash.pal", "wb") as f:
    for i in range(32):
        r = palette[i*3] >> 4
        g = palette[i*3+1] >> 4
        b = palette[i*3+2] >> 4
        word = (r << 8) | (g << 4) | b
        f.write(word.to_bytes(2, "big"))
