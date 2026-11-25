"""Every character the panel may draw in a font is in that font's glyph list.

Text fonts carry GF_Latin_Kernel + GF_Latin_Core (all printable ASCII, so
". - _ ( )" and the rest). The large digit fonts list their glyphs; the
code only hands them strings that hero_safe()/hero_text() accept.
"""
import re
import unittest
from pathlib import Path

FONTS = Path('packages/base/fonts.yaml').read_text()
PANEL = Path('packages/room_companion/panel.yaml').read_text()
DETAIL = Path('onx_panel_detail.h').read_text()


def fonts(text):
    """id -> (glyphs string or None, glyphsets list)."""
    out = {}
    for block in re.split(r'\n  - file:', text):
        m = re.search(r'\bid: (\S+)', block)
        if not m:
            continue
        g = re.search(r'^\s*glyphs: "(.*)"\s*$', block, re.MULTILINE)
        sets = re.findall(r'^\s*- (GF_\w+)', block, re.MULTILINE)
        anchored = '*text_glyphsets' in block or '&text_glyphsets' in block
        out[m.group(1)] = (g.group(1) if g else None, sets or (['GF_Latin_Kernel', 'GF_Latin_Core'] if anchored else []))
    return out


class FontGlyphs(unittest.TestCase):
    def test_text_fonts_cover_printable_ascii(self):
        for font_id, (glyphs, sets) in fonts(FONTS).items():
            if glyphs is not None:
                continue  # A digit font with an explicit list (checked below).
            with self.subTest(font=font_id):
                # No list and no glyphsets: ESPHome defaults to GF_Latin_Kernel.
                self.assertTrue(not sets or 'GF_Latin_Kernel' in sets or 'GF_Latin_Core' in sets)

    def test_hero_font_has_every_character_hero_text_allows(self):
        body = re.search(r'inline bool hero_safe\(.*?\n\}', DETAIL, re.DOTALL).group(0)
        allowed = set(re.findall(r"c == '(.)'", body)) | set('0123456789') | {'°'}
        glyphs, _ = fonts(FONTS)['fonts_MiSans_Medium_64']
        self.assertLessEqual(allowed, set(glyphs), 'hero_safe allows characters the 64 px font lacks')

    def test_big_clock_font_has_time_characters(self):
        glyphs = re.search(r'id: panel_clock_big_font.*?glyphs: "(.*?)"', PANEL, re.DOTALL).group(1)
        self.assertLessEqual(set('0123456789:'), set(glyphs))


if __name__ == '__main__':
    unittest.main()
