# -*- coding: utf-8 -*-
from PIL import Image, ImageFont, ImageDraw

font_path = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
f = ImageFont.truetype(font_path, 15)

W = 8
H = 14

def get_glyph_bytes(char):
    # Create image with character centered
    im = Image.new('L', (32, 32), color=0)
    draw = ImageDraw.Draw(im)
    bbox = f.getbbox(char)
    # bbox = (left, top, right, bottom)
    char_w = bbox[2] - bbox[0]
    char_h = bbox[3] - bbox[1]
    
    # Draw so that baseline is around y = 14
    # let's offset
    ox = max(0, (W - char_w) // 2 - bbox[0])
    oy = max(0, (11 - bbox[3])) # align baseline at y=11
    draw.text((ox, oy), char, font=f, fill=255)
    
    # Extract W x H (0..W-1, 0..H-1)
    bytes_arr = []
    for y in range(H):
        b = 0
        for x in range(W):
            if im.getpixel((x, y)) > 110:
                b |= (1 << (7 - x))
        bytes_arr.append(b)
    return bytes_arr

for c in ['А', 'а', 'р', 'у', 'щ', 'Д', 'й', 'Ё', 'ё', '—']:
    gb = get_glyph_bytes(c)
    print(f"Char '{c}':")
    for b in gb:
        line = "".join("#" if (b & (1 << (7 - x))) else " " for x in range(W))
        print(f"|{line}|")
