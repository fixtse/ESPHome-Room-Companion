// Host tests for onx_glyphs.h: c++ -std=c++17 -Itests/glyph_stubs -I. tests/test_glyphs.cpp
#include "onx_glyphs.h"
#include <cassert>
#include <cstdio>
#include <thread>

static std::vector<uint8_t> record(uint16_t w, uint16_t h, uint16_t ascent) {
  std::vector<uint8_t> v = {uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8),
                            uint8_t(w), uint8_t(w >> 8), uint8_t(ascent), uint8_t(ascent >> 8)};
  v.resize(8 + size_t(w) * h, 0);
  v[8 + size_t(w) * 2 + 1] = 255;  // One lit pixel on row 2.
  return v;
}
static void arrive(uint64_t k) {
  auto data = record(4, 6, 5);
  onx_glyphs::Result r{k, {}, false};
  assert(onx_glyphs::parse(data.data(), data.size(), r.glyph));
  std::lock_guard<std::mutex> g(onx_glyphs::mutex());
  onx_glyphs::results().push_back(r);
}

int main() {
  using namespace onx_glyphs;
  // Parse trims blank rows and keeps the baseline offset.
  {
    auto data = record(4, 6, 5);
    Glyph g;
    assert(parse(data.data(), data.size(), g));
    assert(g.w == 4 && g.h == 1 && g.adv == 4 && g.ofs_y == 5 - 3);
    free(g.a8);
    data.pop_back();
    assert(!parse(data.data(), data.size(), g));
  }
  // Arrivals are stored, bump the generation and refresh the styles once.
  {
    const uint32_t gen = generation();
    arrive(key(0xAC00, 18));
    arrive(key(0xAC01, 18));
    poll(nullptr);
    assert(ready().size() == 2 && generation() == gen + 1 && style_reports == 1);
    poll(nullptr);  // Nothing new: no refresh.
    assert(generation() == gen + 1 && style_reports == 1);
    // A second copy of the same glyph replaces the first.
    arrive(key(0xAC00, 18));
    poll(nullptr);
    assert(ready().size() == 2);
  }
  // A full cache drops the least recently drawn, never ones drawn just now.
  {
    for (auto &e : ready()) free(e.second.a8);
    ready().clear();
    const uint32_t now = now_ms();
    for (uint32_t i = 0; i < GLYPH_LIMIT + 1; ++i) {
      Glyph g;
      g.a8 = static_cast<uint8_t *>(malloc(1));
      g.used = i < 300 ? now - 60000 - i : now;  // 300 old ones, the oldest at the highest i.
      ready()[key(0x4E00 + i, 18)] = g;
    }
    trim();
    assert(ready().size() == GLYPH_KEEP);
    // Kept: everything recent, and the 99 youngest old ones (i < 99).
    for (uint32_t i = 0; i < 300; ++i) assert(ready().count(key(0x4E00 + i, 18)) == (i < 99));
    for (uint32_t i = 300; i < GLYPH_LIMIT + 1; ++i) assert(ready().count(key(0x4E00 + i, 18)));
    // When everything is in use nothing is dropped, even over the limit.
    for (auto &e : ready()) e.second.used = now;
    for (uint32_t i = 0; i < 300; ++i) {
      Glyph g; g.used = now; ready()[key(0x5E00 + i, 18)] = g;
    }
    const size_t before = ready().size();
    trim();
    assert(ready().size() == before);
  }
  // Lookups mark glyphs as used.
  {
    lv_font_t font{};
    font.user_data = reinterpret_cast<void *>(uintptr_t(18));
    ready()[key(0xB098, 18)] = Glyph{nullptr, 0, 0, 7, 0, 0};
    lv_font_glyph_dsc_t dsc{};
    assert(get_glyph_dsc(&font, &dsc, 0xB098, 0) && dsc.adv_w == 7);
    assert(now_ms() - ready()[key(0xB098, 18)].used < 1000);
    assert(!get_glyph_dsc(&font, &dsc, 'A', 0));  // Not worth asking for.
  }
  // UTF-8 decoding.
  {
    const std::string s = "a\xED\x95\x9C\xE4\xB8\x80";  // a, 한, 一
    size_t i = 0;
    assert(next_utf8(s, i) == 'a' && next_utf8(s, i) == 0xD55C && next_utf8(s, i) == 0x4E00 && i == s.size());
  }
  std::puts("glyph tests passed");
}
