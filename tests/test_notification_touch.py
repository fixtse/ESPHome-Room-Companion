"""Notification header targets must consume every pixel of their LVGL hit area."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class NotificationTouch(unittest.TestCase):
    def test_header_passthrough(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'onx_notification_ui.h').read_text()
        hits = source[source.index('inline bool bell_hit('):source.index('// Called by the panel\'s UI tick: the bell belongs')]
        code = r'''
#include <cassert>
#include <cmath>
#include "my_components/onx_touch_input/onx_touch_input.h"
struct lv_area_t{int x1,y1,x2,y2;};
struct lv_obj_t{lv_area_t area;bool visible;};
bool lv_obj_is_visible(lv_obj_t *o){return o->visible;}
void lv_obj_get_coords(lv_obj_t *o,lv_area_t *a){*a=o->area;}
namespace onx_notification_ui {
int S(int n){return int(std::lround(n*320.0/240));}
lv_obj_t *bell=nullptr,*timer_chip=nullptr,*media_chip=nullptr;
HITS
}
int main(){
 using namespace onx_notification_ui;
 lv_obj_t icon{{272,2,315,37},true},chip{{180,1,260,38},true};bell=&icon;timer_chip=&chip;
 // All four corners, especially y<2 and scaled padding beyond x=264.
 for(int x=icon.area.x1-S(8);x<=icon.area.x2+S(8);++x)
  for(int y=icon.area.y1-S(8);y<=icon.area.y2+S(8);++y)assert(bell_hit(x,y));
 assert(!bell_hit(icon.area.x1-S(8)-1,20));
 assert(!bell_hit(300,icon.area.y2+S(8)+1));
 icon.visible=false;assert(!bell_hit(300,20));icon.visible=true;
 assert(timer_chip_hit(180-S(6),1-S(6)));
 chip.visible=false;assert(!timer_chip_hit(200,20));chip.visible=true;
 esphome::onx_touch_input::TouchInput touch;
 esphome::onx_button::BufferedButton button;touch.set_button(&button);touch.set_geometry(320,436,54,29);
 touch.set_passthrough_fn([](int x,int y){return bell_hit(x,y)||timer_chip_hit(x,y);});
 auto tap=[&](int x,int y){touch.update({{x,y,esphome::touchscreen::STATE_PRESSED}});touch.release();touch.loop();};
 tap(300,0);tap(262,20);tap(300,46);tap(175,0);tap(200,20);
 assert(button.edges.empty()); // No underlying entity opens from any header tap.
 tap(150,220);assert(button.edges.size()==2); // Ordinary entity taps still work.
}
'''.replace('HITS', hits)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'touch.cpp'
            exe = Path(directory) / 'touch'
            cpp.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-I', str(root), '-I', str(root / 'tests/touch_stubs'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
