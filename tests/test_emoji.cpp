#include "../onx_emoji.h"
#include <cassert>
int main() {
  assert(onx_emoji::encode("Beyoncé") == "Beyoncé");
  for (const char *text : {"👨‍👩‍👧‍👦", "👩🏽‍💻", "👍🏿", "🇵🇪", "🏳️‍🌈", "1️⃣"}) {
    auto encoded = onx_emoji::encode(text);
    assert(encoded.size() == 4);
  }
  assert(onx_emoji::encode("❤️") == onx_emoji::encode("❤"));
  lv_font_t font{24, 0, nullptr};
  for (unsigned i = 0; i < sizeof(onx_emoji::GLYPHS) / sizeof(onx_emoji::GLYPHS[0]); ++i) {
    assert(onx_emoji::encode(onx_emoji::GLYPHS[i].sequence).size() == 4);
    // Exercise every compressed atlas record without retaining an unbounded test cache.
    const auto *image = static_cast<const lv_image_dsc_t *>(onx_emoji::source(&font, onx_emoji::FIRST_GLYPH + i, 0, nullptr, nullptr));
    assert(image);
    assert(image->data_size == 24 * 24 * 4);
    free(const_cast<uint8_t *>(image->data));
    onx_emoji::cache.clear(); onx_emoji::cache_bytes = 0;
  }
  for (unsigned i = 0; i < 240; ++i)
    assert(onx_emoji::source(&font, onx_emoji::FIRST_GLYPH + i, 0, nullptr, nullptr));
  assert(onx_emoji::cache_bytes > onx_emoji::CACHE_LIMIT);
  onx_emoji::trim_cache(nullptr);
  assert(onx_emoji::cache.empty() && onx_emoji::cache_bytes == 0);
}
