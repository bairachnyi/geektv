# -*- coding: utf-8 -*-
from PIL import Image, ImageFont, ImageDraw

font_path = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
font_size = 14
f = ImageFont.truetype(font_path, font_size)

# We want 8 width x 14 height bitmaps for each character
W = 8
H = 14

def render_char(char):
    im = Image.new('L', (16, 20), color=0)
    draw = ImageDraw.Draw(im)
    draw.text((1, 0), char, font=f, fill=255)
    
    # Crop to W x H
    # Let's find vertical bounds
    res = Image.new('L', (W, H), color=0)
    res.paste(im.crop((1, 2, 1 + W, 2 + H)), (0, 0))
    return res

test_chars = ['А', 'Б', 'В', 'Г', 'Д', 'а', 'б', 'в', 'г', 'д', 'я', 'ю', '—', '"', '!']
for c in test_chars[:5]:
    im = render_char(c)
    print(f"Char '{c}':")
    for y in range(H):
        line = "".join("#" if im.getpixel((x, y)) > 100 else " " for x in range(W))
        print(f"|{line}|")
