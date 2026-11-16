"""Generate color glyphs and sequence mappings from Noto Color Emoji (OFL)."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

font_path = sys.argv[1]
font = ImageFont.truetype(font_path, 109)
source = TTFont(font_path)
reverse = {name: chr(code) for code, name in source.getBestCmap().items()}
sequences = set()
for lookup in source['GSUB'].table.LookupList.Lookup:
    for table in lookup.SubTable:
        for first, ligatures in getattr(table, 'ligatures', {}).items():
            for ligature in ligatures:
                names = [first, *ligature.Component]
                if all(name in reverse for name in names):
                    sequences.add(''.join(reverse[name] for name in names))
# Only emoji scalars, excluding controls, joiners, selectors and tag characters.
sequences.update(chr(c) for c in source.getBestCmap() if
                 0x1F000 <= c <= 0x1FAFF or 0x2300 <= c <= 0x2BFF or c in (0xA9, 0xAE, 0x203C, 0x2049, 0x2122, 0x2139, 0x3030, 0x303D, 0x3297, 0x3299))
sequences = sorted({sequence.replace('\ufe0f', '') for sequence in sequences})

def packed(data):
    result = bytearray()
    at = 0
    while at < len(data):
        run = 1
        while at + run < len(data) and data[at + run] == data[at] and run < 128:
            run += 1
        if run >= 3:
            result.extend((127 + run, data[at])); at += run
        else:
            start = at
            at += run
            while at < len(data) and at - start < 128:
                if at + 2 < len(data) and data[at] == data[at+1] == data[at+2]:
                    break
                at += 1
            result.append(at - start - 1); result.extend(data[start:at])
    return result

lines = ['// Generated from Noto Color Emoji; SIL OFL 1.1.', '#pragma once', 'namespace onx_emoji {', 'inline constexpr unsigned ATLAS_SIZE = 24;', 'struct Glyph { const char *sequence; const uint8_t *pixels; unsigned length; };']
entries = []
buffers = {}
for index, sequence in enumerate(sequences):
    im = Image.new('RGBA', (1200, 160))
    ImageDraw.Draw(im).text((0, 0), sequence, font=font, embedded_color=True)
    bounds = im.getbbox()
    if not bounds:
        continue
    im = im.crop(bounds)
    # Unshaped/unsupported sequences render as multiple adjacent glyphs.
    if im.width > im.height * 1.8:
        continue
    im = im.resize((24, 24), Image.Resampling.LANCZOS)
    quantized = im.quantize(colors=32)
    palette = quantized.getpalette('RGBA')
    data = bytearray()
    for offset in range(0, 32 * 4, 4):
        r, g, b, a = palette[offset:offset + 4]
        rgb = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        data.extend((rgb & 255, rgb >> 8, a) if a else (0, 0, 0))
    data.extend(quantized.tobytes())
    encoded = packed(data)
    key = bytes(encoded)
    if key not in buffers:
        buffers[key] = index
        lines.append(f'inline const uint8_t pixels_{index}[] = {{'+','.join(map(str, encoded))+'};')
    buffer_index = buffers[key]
    literal = ''.join(f'\\x{v:02x}' for v in sequence.encode())
    entries.append((sequence, f'{{"{literal}",pixels_{buffer_index},{len(encoded)}}}'))
# Sorted UTF-8 keys support binary prefix lookup at runtime.
entries.sort(key=lambda entry: entry[0].encode())
lines.append('inline const Glyph GLYPHS[] = {' + ',\n'.join(entry for _, entry in entries) + '};\n}')
Path('onx_emoji_pixels.h').write_text('\n'.join(lines)+'\n')
print(f'{len(entries)} glyphs; {sum(len(line) for line in lines)} source bytes')
