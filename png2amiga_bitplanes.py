from PIL import Image
import numpy as np

def to_amiga_rgb12(color):
    """Convert 8-bit RGB to Amiga 12-bit RGB (0x0BGR format, 4 bits each)"""
    r, g, b = [c >> 4 for c in color[:3]]
    return (b << 8) | (g << 4) | (r)

def write_plane_c(name, plane):
    """Dump a C array for one bitplane"""
    print(f"unsigned char {name}[10240] = {{")
    for i in range(0, len(plane), 16):
        row = ", ".join(f"0x{b:02x}" for b in plane[i:i+16])
        print(f"    {row},")
    print("};\n")

def main():
    # === Input PNG: must be 320x256, 32-color paletted ===
    img = Image.open("splash.png").convert("P")
    if img.size != (320, 256):
        raise Exception("Image must be 320x256 pixels!")
    # Make sure palette is <= 32 colors
    palette = img.getpalette()[:32*3]
    # Get used palette indices
    used = sorted(set(img.getdata()))
    # Remap indices if palette is not packed
    pal_map = {old: new for new, old in enumerate(used)}
    data = np.array([pal_map[p] for p in img.getdata()]).reshape((256, 320))

    # Build Amiga palette (RGB12)
    amiga_palette = []
    for i in used:
        rgb = palette[i*3:i*3+3]
        amiga_palette.append(to_amiga_rgb12(rgb))
    # Pad to 32 colors
    while len(amiga_palette) < 32:
        amiga_palette.append(0x000)

    print("// --- NeoBench 32-color splash palette ---")
    print("unsigned short splash_palette[32] = {")
    for i, c in enumerate(amiga_palette):
        print(f"    0x{c:03x},", end='\n' if (i+1)%8==0 else ' ')
    print("};\n")

    # --- Export 5 bitplanes ---
    print("// --- NeoBench splash 5 bitplanes ---")
    # Each plane: 320x256 bits => 10240 bytes
    for plane in range(5):
        bparr = bytearray()
        for row in data:
            for x in range(0, 320, 8):
                val = 0
                for b in range(8):
                    if x + b < 320:
                        idx = row[x+b]
                        if (idx >> plane) & 1:
                            val |= (0x80 >> b)
                bparr.append(val)
        write_plane_c(f"plane{plane}", bparr)

if __name__ == "__main__":
    main()
