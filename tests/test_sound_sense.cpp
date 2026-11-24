#include "../onx_sound_sense.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <limits>
int main() {
  using namespace onx_sound_sense;
  Meter meter;
  std::vector<int16_t> pcm(256, 16384);  // Half full-scale RMS, including negative polarity.
  auto feed = [&](uint32_t now) { meter.feed(reinterpret_cast<uint8_t *>(pcm.data()), pcm.size()*2, now); };
  feed(1000);
  assert(std::isnan(meter.level(1000))); // Disabled capture cannot publish microphone data.
  meter.capture = true;
  feed(1000);
  assert(std::abs(meter.level(1000) - 83.9794f) < 0.01f);
  std::fill(pcm.begin(),pcm.end(),-16384);
  feed(1100);
  assert(std::abs(meter.level(1100) - 83.9794f) < 0.01f);
  assert(std::isnan(meter.level(2601))); // Offline/stopped microphone is unknown, not silence.
  std::fill(pcm.begin(),pcm.end(),0);
  feed(3000);
  assert(meter.level(3000) == 0);
  meter.capture = false;
  assert(std::isnan(meter.level(3000)));
  meter.reset();meter.capture = true;
  assert(std::isnan(meter.level(3000)));
  feed(UINT32_MAX-100);
  assert(meter.level(50)==0); // Freshness survives millis rollover.
  assert(std::isnan(meter.level(50, 1)) && std::isnan(meter.correlation(50))); // Mono has no pair.
  // Stereo: left at half scale, right at quarter scale, right delayed by one
  // sample (the sound reached the left microphone first).
  {
    Meter stereo;
    stereo.capture = true;
    std::vector<int16_t> frames(2 * 512);
    for (size_t f = 0; f < 512; ++f) {
      auto wave = [](long n) { return std::sin(2 * M_PI * 1000.0 * double(n) / 16000.0); };
      frames[2 * f] = int16_t(16384 * wave(long(f)));
      frames[2 * f + 1] = int16_t(8192 * wave(long(f) - 1));
    }
    stereo.feed(reinterpret_cast<uint8_t *>(frames.data()), frames.size() * 2, 1000, 2);
    const float left = stereo.level(1000, 0), right = stereo.level(1000, 1);
    assert(std::abs(left - right - 6.02f) < 0.1f);
    assert(stereo.level(1000) == right);  // Ambient follows the Assist (right) microphone.
    assert(stereo.correlation(1000) > 0.85f && stereo.correlation(1000) < 0.95f);  // cos(22.5°)
    assert(std::abs(stereo.lag_samples(1000) - 1.0f) < 0.25f);
    stereo.capture = false;
    assert(std::isnan(stereo.lag_samples(1000)));
  }
  Detector detector;
  auto state = [&](float db,uint32_t now){return std::string(detector.update(db,30,50,now));};
  assert(state(10,1000)=="quieting");
  assert(state(10,15999)=="quieting");
  assert(state(10,16000)=="silence");
  assert(state(60,17000)=="noise");
  assert(state(60,25001)=="active");
  assert(state(60,29000)=="presence");
  assert(state(NAN,30000)=="unknown");
  assert(state(60,31000)=="active"); // Playback/mute gaps reset presence history.
  assert(state(60,43000)=="presence");
  assert(state(10,44000)=="quieting");
  assert(volume_target(0,15,5)==0.15f);
  assert(volume_target(90,15,0)==0.15f);
  assert(volume_target(90,15,10)==1);
  assert(std::isnan(volume_target(NAN,15,5)));
  assert(volume_target(100,100,10)==1);
}
