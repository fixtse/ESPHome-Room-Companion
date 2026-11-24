#include "../my_components/onx_mic_select/selector.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace onx_mic_selector;

namespace {
constexpr size_t kFrames = 256;  // 16 ms at 16 kHz.
std::mt19937 rng(7);

// One chunk: independent noise on each mic plus an optional tone ("speech")
// with a per-mic amplitude.
std::vector<int16_t> chunk(float noise_right, float noise_left, float speech_right, float speech_left, long &t) {
  std::normal_distribution<float> n(0.0f, 1.0f);
  std::vector<int16_t> s(2 * kFrames);
  for (size_t f = 0; f < kFrames; ++f, ++t) {
    const float tone = std::sin(2 * M_PI * 500.0 * double(t) / 16000.0);
    s[2 * f + kRightMic] = int16_t(std::lround(noise_right * n(rng) + speech_right * tone));
    s[2 * f + kLeftMic] = int16_t(std::lround(noise_left * n(rng) + speech_left * tone));
  }
  return s;
}

// Alternates 1 s of background with 1 s of speech, for `seconds`.
void run(Selector &sel, float seconds, float nr, float nl, float sr, float sl, long &t) {
  std::vector<int16_t> out(kFrames);
  const int chunks = int(seconds * 1000 / 16);
  for (int i = 0; i < chunks; ++i) {
    const bool speaking = (i / 62) % 2 == 1;
    auto in = chunk(nr, nl, speaking ? sr : 0, speaking ? sl : 0, t);
    sel.process(in.data(), kFrames, out.data());
    for (size_t f = 0; f < kFrames; ++f) {
      const int16_t a = in[2 * f], b = in[2 * f + 1];
      assert(out[f] >= std::min(a, b) - 1 && out[f] <= std::max(a, b) + 1);  // Never louder than both.
    }
  }
}
}  // namespace

int main() {
  long t = 0;
  {
    // Matched mics (right 0.7 dB more sensitive, same SNR): keeps the left mic.
    Selector sel;
    run(sel, 20, 108, 100, 1080, 1000, t);
    assert(sel.channel() == kLeftMic);
    assert(sel.switches() == 0);
  }
  {
    // The right mic hears the talker 6 dB better over the same noise: switches once.
    Selector sel;
    run(sel, 10, 100, 100, 2000, 1000, t);
    assert(sel.channel() == kRightMic);
    assert(sel.switches() == 1);
    // ...and back when the left one becomes clearly better.
    run(sel, 10, 100, 100, 1000, 2000, t);
    assert(sel.channel() == kLeftMic);
    assert(sel.switches() == 2);
  }
  {
    // The left mic goes silent (disconnected): the right one takes over in ~1 s.
    Selector sel;
    run(sel, 2, 100, 100, 1000, 1000, t);
    assert(sel.channel() == kLeftMic);
    run(sel, 0.9f, 100, 0, 1000, 0, t);
    assert(sel.channel() == kLeftMic);
    run(sel, 0.5f, 100, 0, 1000, 0, t);
    assert(sel.channel() == kRightMic && sel.dead(kLeftMic));
  }
  {
    // A forced choice wins over the scores.
    Selector sel;
    sel.mode = MODE_RIGHT;
    run(sel, 2, 100, 100, 1000, 4000, t);
    assert(sel.channel() == kRightMic);
    sel.mode = MODE_AUTO;
    run(sel, 4, 100, 100, 1000, 4000, t);
    assert(sel.channel() == kLeftMic);
  }
  {
    // A quiet room with no sound above the floor never switches.
    Selector sel;
    run(sel, 10, 100, 60, 0, 0, t);
    assert(sel.channel() == kLeftMic && sel.switches() == 0);
  }
  return 0;
}
