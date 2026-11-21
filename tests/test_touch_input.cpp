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
  down(270,100); move(200,100); move(20,100); touch.release();touch.loop();
  assert(turns==1 && backs==0 && button.edges.empty()); // One page per swipe.
  down(30,100); move(150,100); touch.release();touch.loop(); assert(turns==0);
  detail=true;
  down(200,100); move(100,100); touch.release();touch.loop();
  down(40,100); move(200,100); touch.release();touch.loop();
  assert(backs==2 && turns==0); // Both directions close without changing a value.
  down(100,100); test_ms+=1000; touch.loop(); touch.release();touch.loop();
  assert(backs==2 && button.edges.size()==2); // Holding never generates a long press.
  button.edges.clear(); native=true;
  down(100,200); move(100,60); touch.release();touch.loop();
  assert(turns==0 && button.edges.empty() && touch.suppress_click()); // Native scroll, no accidental click.
  native=false; detail=false;
  down(214,458); touch.release();touch.loop(); assert(jumps==1 && button.edges.empty());
  down(160,458); touch.release();touch.loop(); assert(jumps==1 && button.edges.empty()); // Selected nav icon is informational.
  // Rebuild guards cover passthrough taps (which have State::IGNORED),
  // not only sliders/gestures. Retain widgets through delayed LVGL release.
  test_ms += 200;
  assert(!touch.interaction_pending());
  touch.set_passthrough_fn([](int, int) { return true; });
  down(160,200);
  assert(touch.interaction_pending());
  test_ms += 2000;
  assert(touch.interaction_pending());
  touch.release();touch.loop();
  assert(touch.interaction_pending());
  test_ms += 179;
  assert(touch.interaction_pending());
  ++test_ms;
  assert(!touch.interaction_pending());
  // A drag beginning on an LVGL transport button cannot trigger playback.
  touch.set_passthrough_fn([](int x,int y) { return x>=100 && x<=200 && y>=300; });
  test_ms+=200;
  down(160,330); move(110,330); touch.release();touch.loop();
  assert(touch.suppress_click() && button.edges.empty() && turns==0);
  test_ms+=20; // A fresh intended tap must not inherit the previous swipe guard.
  down(160,330); touch.release();touch.loop();
  assert(!touch.suppress_click() && button.edges.empty()); // An ordinary tap remains LVGL's.
  assert(touch.interaction_pending());
  touch.acknowledge_click();
  assert(!touch.interaction_pending()); // No extra 180ms wait after the click was dispatched.
  // Timer-chip passthrough must never emit the underlying virtual entity press.
  touch.set_passthrough_fn([](int x,int y){return x>=180&&x<270&&y<42;});
  down(230,22);touch.release();touch.loop();assert(button.edges.empty());
  // Composed cards intercept taps without swallowing carousel swipes or holds.
  touch.set_passthrough_fn([](int,int){return false;});
  int card_taps=0;
  touch.set_tap_fn([&](int x,int y){if(y>56 && y<436){++card_taps;return true;}return false;});
  test_ms+=200;down(150,200);touch.release();touch.loop();assert(card_taps==1 && button.edges.empty());
  test_ms+=200;down(270,200);move(160,200);touch.release();touch.loop();assert(card_taps==1 && turns==1);
  test_ms+=200;down(150,200);test_ms+=1000;touch.loop();touch.release();touch.loop();
  assert(card_taps==1 && button.edges.size()==2);
  // Detail controls next to the left edge never block the back swipe.
  button.edges.clear(); detail=true;
  touch.set_edge_back([&]{return true;});
  touch.set_passthrough_fn([](int x,int){return x<120;});
  test_ms+=200;down(10,200);move(150,200);touch.release();touch.loop();
  assert(backs==3 && turns==1);
  test_ms+=200;down(60,200);move(200,200);touch.release();touch.loop();
  assert(backs==3 && turns==1); // Away from the edge the control keeps the gesture.
  touch.set_swipe_left_back([&]{return detail;});
  for(int x:{110,270}) {
    test_ms+=200;down(x,200);move(x-65,200);
    assert(touch.suppress_click()); // Cancel release on a temperature dial.
    touch.release();touch.loop();
  }
  assert(backs==5 && turns==1 && button.edges.empty());
  detail=false;
  touch.set_passthrough_fn([](int,int){return false;});
  int grid_holds=0;
  touch.set_hold_fn([&](int,int){++grid_holds;return true;});
  test_ms+=200;down(150,200);test_ms+=1000;touch.loop();touch.release();touch.loop();
  assert(grid_holds==1 && card_taps==1 && button.edges.empty());
  test_ms+=200;down(150,200);touch.release();touch.loop();
  assert(grid_holds==1 && card_taps==2 && button.edges.empty());

  int dock_calls = 0, dock_offset = 0;
  touch.set_dock_scroll([&](int steps) {
    ++dock_calls;
    dock_offset = std::clamp(dock_offset + steps, 0, 3);
  });
  down(270, 458);
  move(135, 458); touch.loop();  // Scrolls run from the component loop.
  assert(dock_calls == 1 && dock_offset == 3);  // Five steps arrive in one sample.
  move(134, 458); touch.loop();
  assert(dock_calls == 1);
  move(162, 458); touch.loop();
  assert(dock_calls == 2 && dock_offset == 2);  // Reversing exits the bound.
  touch.release();touch.loop();
  assert(turns == 1 && jumps == 1 && button.edges.empty());

  // A list scroll that reaches its end hands the same swipe to the close drag.
  bool list_at_end=false, handed=false; int drag_dy=0, release_dy=1;
  touch.set_enabled_lambda([] { return false; });
  touch.set_pull_up_enabled([&] { return list_at_end; });
  touch.set_pull_up_drag([&](int dy) { drag_dy=dy; }, [&](int dy, uint32_t) { release_dy=dy; return false; });
  touch.set_pull_up_handoff([&] { handed=true; });
  test_ms+=200; down(160,300); move(160,250);
  assert(!handed && drag_dy==0);           // The list scrolls first.
  list_at_end=true; move(160,200);
  assert(handed && drag_dy==0);            // Hand-off starts the drag at zero travel.
  move(160,140); assert(drag_dy==-60);
  touch.release();touch.loop(); assert(release_dy==-60);

  // Horizontal page drag: live dx updates, cancel on short drag, commit on drag/flick.
  int page_dx = 0, page_turns = 0;
  touch.set_enabled_lambda([] { return true; });
  touch.set_pull_up_enabled([] { return false; });
  touch.set_page_swipe([] { return true; });
  touch.set_back_enabled([] { return false; });
  touch.set_native_touch([] { return false; });
  touch.set_passthrough_fn([](int, int) { return false; });
  touch.get_turn_trigger()->callback = [&](int d) { page_turns += d; };
  touch.set_page_drag(
      [&](int dx) { page_dx = dx; },
      [&](int dx, uint32_t ms) { return std::abs(dx) >= 36 || (ms < 280 && std::abs(dx) >= 20); });
  test_ms += 200;
  down(160, 200); move(140, 200); test_ms += 400; touch.release(); touch.loop();
  assert(page_dx == -20 && page_turns == 0);  // Slow short drag cancels cleanly.
  test_ms += 200;
  down(160, 200); move(135, 200); test_ms += 100; touch.release(); touch.loop();
  assert(page_dx == -25 && page_turns == 1);  // Quick flick commits next page.
  test_ms += 200;
  down(100, 200); move(150, 200); test_ms += 500; touch.release(); touch.loop();
  assert(page_dx == 50 && page_turns == 0);   // Deliberate right drag commits previous page.
}
