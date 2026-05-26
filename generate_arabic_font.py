#!/usr/bin/env python3
"""
Convert TTF font to Adafruit GFX format
"""
import sys
import freetype

def convert_font(filename, size, output_file):
    face = freetype.Face(filename)
    face.set_pixel_sizes(0, size)
    
    chars_to_include = list(range(0x0020, 0x002A)) + list(range(0x0600, 0x0700))
    
    glyphs = {}
    
    for char_code in chars_to_include:
        try:
            face.load_char(char_code, freetype.FT_LOAD_RENDER)
            slot = face.glyph
            bitmap = slot.bitmap
            if bitmap.width > 0 and bitmap.rows > 0:
                bits = []
                for y in range(bitmap.rows):
                    row = 0
                    for x in range(bitmap.width):
                        pos = y * bitmap.width + x
                        if pos < len(bitmap.buffer) and bitmap.buffer[pos] > 127:
                            row |= (1 << (7 - (x % 8)))
                    bits.append(row)
                glyphs[char_code] = {
                    'width': bitmap.width,
                    'height': bitmap.rows,
                    'advance': slot.advance.x >> 6,
                    'bitmap': bits,
                    'left': slot.bitmap_left,
                    'top': slot.bitmap_top
                }
        except:
            pass
    
    if not glyphs:
        print("No glyphs generated!")
        return
    
    first_char = min(glyphs.keys())
    last_char = max(glyphs.keys())
    
    font_name = "Arabi%dpt7b" % size
    bitmap_name = "%sBitmaps" % font_name
    glyph_name = "%sGlyphs" % font_name
    
    with open(output_file, 'w') as f:
        f.write('// Noto Naskh Arabic %dpt for Adafruit GFX\n\n' % size)
        f.write('#pragma once\n\n')
        f.write('#include <Arduino.h>\n')
        f.write('#include <Adafruit_GFX.h>\n\n')
        
        # Bitmap array
        f.write('const uint8_t %s[] PROGMEM = {\n' % bitmap_name)
        for char_code in sorted(glyphs.keys()):
            g = glyphs[char_code]
            for row in g['bitmap']:
                f.write('0x%02X, ' % row)
            f.write(' // 0x%04X\n' % char_code)
        f.write('};\n\n')
        
        # Glyphs array
        f.write('const GFXglyph %s[] = {\n' % glyph_name)
        for char_code in sorted(glyphs.keys()):
            g = glyphs[char_code]
            # Convert -1 to 0 for bitmap_left (must be unsigned)
            left = max(0, g['left'])
            f.write('  {%d, %d, %d, %d, %d}, // 0x%04X\n' % (
                g['width'], g['height'], left, g['top'], g['advance'], char_code
            ))
        f.write('};\n\n')
        
        # Font struct
        f.write('const GFXfont %s = {\n' % font_name)
        f.write('  (uint8_t*)%s,\n' % bitmap_name)
        f.write('  (GFXglyph*)%s,\n' % glyph_name)
        f.write('  0x%02X, 0x%02X, %d,\n' % (first_char, last_char, size))
        f.write('};\n')
    
    print("Generated %s: %d glyphs" % (output_file, len(glyphs)))

if __name__ == '__main__':
    if len(sys.argv) < 4:
        print("Usage: fontgen.py <font.ttf> <size> <output.h>")
        sys.exit(1)
    convert_font(sys.argv[1], int(sys.argv[2]), sys.argv[3])