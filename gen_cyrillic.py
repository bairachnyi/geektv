# -*- coding: utf-8 -*-
from PIL import Image, ImageFont, ImageDraw

font_path = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
f = ImageFont.truetype(font_path, 15)

W = 8
H = 14

def get_glyph_bytes(char):
    im = Image.new('L', (32, 32), color=0)
    draw = ImageDraw.Draw(im)
    bbox = f.getbbox(char)
    if not bbox:
        return [0]*H
    char_w = bbox[2] - bbox[0]
    char_h = bbox[3] - bbox[1]
    
    ox = max(0, (W - char_w) // 2 - bbox[0])
    oy = max(0, (11 - bbox[3])) # baseline at 11
    draw.text((ox, oy), char, font=f, fill=255)
    
    bytes_arr = []
    for y in range(H):
        b = 0
        for x in range(W):
            if im.getpixel((x, y)) > 110:
                b |= (1 << (7 - x))
        bytes_arr.append(b)
    return bytes_arr

# 0..63: U+0410 .. U+044F (А..Я, а..я)
glyphs = []
for cp in range(0x0410, 0x0450):
    glyphs.append((cp, chr(cp), get_glyph_bytes(chr(cp))))

# 64: Ё (U+0401)
glyphs.append((0x0401, 'Ё', get_glyph_bytes('Ё')))
# 65: ё (U+0451)
glyphs.append((0x0451, 'ё', get_glyph_bytes('ё')))
# 66: — (U+2014)
glyphs.append((0x2014, '—', get_glyph_bytes('—')))

print(f"Generated {len(glyphs)} glyphs.")

# Write C header
with open('src/features/clock/CyrillicFont8x14.h', 'w', encoding='utf-8') as out:
    out.write('#pragma once\n#include <Arduino.h>\n\n')
    out.write('// Compact 8x14 Cyrillic font bitmaps for quotes display (14 bytes per character)\n')
    out.write(f'// Total glyphs: {len(glyphs)} ({len(glyphs)*14} bytes in PROGMEM)\n')
    out.write(f'static const uint8_t CYRILLIC_FONT_8X14[{len(glyphs)}][14] PROGMEM = {{\n')
    for idx, (cp, ch, gb) in enumerate(glyphs):
        hex_str = ", ".join(f"0x{b:02X}" for b in gb)
        out.write(f'  {{ {hex_str} }}, // [{idx:2d}] U+{cp:04X} \'{ch}\'\n')
    out.write('};\n\n')
    out.write(r'''
// Map UTF-8 codepoint to index in CYRILLIC_FONT_8X14, or -1 if not found.
static inline int cyrillicGlyphIndex(uint32_t cp) {
  if (cp >= 0x0410 && cp <= 0x044F) return (int)(cp - 0x0410); // А..Я, а..я (0..63)
  if (cp == 0x0401) return 64; // Ё
  if (cp == 0x0451) return 65; // ё
  if (cp == 0x2014) return 66; // em-dash —
  return -1;
}
''')
print("Successfully wrote src/features/clock/CyrillicFont8x14.h")
