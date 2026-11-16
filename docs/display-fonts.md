# Display character support

Text fonts (12–32 px) explicitly include `GF_Latin_Kernel` and `GF_Latin_Core`.
This retains ordinary English text while adding accented Latin letters used in
European and American languages, including `Beyoncé`, `Mañana`, `München`, and
`São Paulo`. Large clock fonts retain their smaller original character set.

Text fonts also include a bounded set of monochrome emoji from Noto Emoji:

❤ ☀ ☁ ☂ ☕ ❄ ⚡ ✅ ❌ ⭐ 🌙 🌞 🌧 🌡 🌈 🎵 🎶 🎧 🎤 🎸 🎹 🎷 🎺 🎻 🥁 📻 💿 📀
🔊 🔇 🔔 🔥 💡 🏠 😀 😊 😂 😍 😎 😢 😭 👍 👏 💙 💚 💛 💜 🎉

Dynamic labels updated through `onx_render::label` render Noto Color Emoji glyphs,
including joined families and professions, skin-tone variants, country flags,
subdivision flags, and keycaps. Matching uses the longest supported UTF-8
sequence before LVGL lays out the text; ordinary text retains its existing font.
Emoji presentation selectors are normalized during matching. Coverage follows the
installed Noto Color Emoji font used to generate the atlas, rather than arbitrary
future Unicode sequences. Unknown sequences retain their text fallback.

## Font source and license

`fonts/NotoEmoji.ttf` is the monochrome variable Noto Emoji font from Google Fonts,
vendored at commit `8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`:
https://github.com/google/fonts/tree/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notoemoji

Its SIL Open Font License is retained in `fonts/OFL-NotoEmoji.txt`.

The checked-in atlas contains 5,575 glyphs at 24 px, with 32 RGBA palette
entries per glyph and lossless run-length compression of the palette/index data.
RGB565 colors and alpha expand to LVGL 9 ARGB8888 images in PSRAM on demand.
Decoded variants use a 512 KiB cache target. When that target is exceeded,
the cache is retired after the current frame completes, allowing later glyphs
to load without invalidating pending draws. Allocation failure falls back to
LVGL's missing-glyph behavior.

Regenerate with `python scripts/generate_color_emoji.py /path/to/NotoColorEmoji.ttf`
(Pillow with color-font shaping and fontTools required). The font itself is not
needed during ESPHome compilation. Run `g++ -std=c++17 -Itests/emoji_stubs
tests/test_emoji.cpp -o /tmp/test-emoji && /tmp/test-emoji` for sequence and atlas
integrity tests.
