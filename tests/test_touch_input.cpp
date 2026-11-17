#include <cassert>
#include "../my_components/onx_touch_input/onx_touch_input.h"
using namespace esphome;
int main() {
  onx_touch_input::TouchInput touch;
  onx_button::BufferedButton button;
  touch.set_button(&button);
  touch.set_geometry(320, 436, 54, 29);
  bool detail=false, native=false;
  touch.set_back_enabled([&] { return detail; });
  touch.set_native_touch([&] { return native; });
  touch.set_page_swipe([&] { return !detail; });
  int turns=0, backs=0, jumps=0;
  touch.get_turn_trigger()->callback=[&](int direction) { turns+=direction; };
  touch.get_back_trigger()->callback=[&] { ++backs; };
  touch.get_strip_trigger()->callback=[&](int offset) { jumps+=offset; };
  auto down=[&](int x,int y) { touch.update({{x,y,touchscreen::STATE_PRESSED}}); };
  auto move=[&](int x,int y) { touch.update({{x,y,touchscreen::STATE_UPDATED}}); };
  down(270,100); move(200,100); move(20,100); touch.release();
  assert(turns==1 && backs==0 && button.edges.empty()); // One page per swipe.
  down(30,100); move(150,100); touch.release(); assert(turns==0);
  detail=true;
  down(200,100); move(100,100); touch.release();
  down(40,100); move(200,100); touch.release();
  assert(backs==2 && turns==0); // Both directions close without changing a value.
  down(100,100); test_ms+=1000; touch.loop(); touch.release();
  assert(backs==2 && button.edges.size()==2); // Holding never generates a long press.
  button.edges.clear(); native=true;
  down(100,200); move(100,60); touch.release();
  assert(turns==0 && button.edges.empty() && touch.suppress_click()); // Native scroll, no accidental click.
  native=false; detail=false;
  down(214,458); touch.release(); assert(jumps==1 && button.edges.empty());
  down(160,458); touch.release(); assert(jumps==1 && button.edges.empty()); // Selected nav icon is informational.
}
