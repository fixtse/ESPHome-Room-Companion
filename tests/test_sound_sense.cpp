#include "../onx_sound_sense.h"
#include <cassert>
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
