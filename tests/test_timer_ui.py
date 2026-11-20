"""Exercise production timer retirement and stable widget updates without a panel."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class TimerUI(unittest.TestCase):
    def test_timer_lifecycle_and_widgets(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'onx_panel_detail.h').read_text()
        state = source[source.index('struct LocalTimer {'):source.index('inline std::function<void(const std::string &)> stop_local_timer;')]
        item = source[source.index('struct TimerItem {'):source.index('inline TimerItem entity_timer(')]
        local = source[source.index('inline std::vector<TimerItem> local_items()'):source.index('// Every timer the panel knows about:')]
        update = source[source.index('inline void tick_timers()'):source.index('// The hero font carries only digits')]
        code = r'''
#include "onx_timer_state.h"
#include <vector>
#include <cassert>
struct lv_obj_t { std::string text; int progress=0,width=0; bool hidden=false; };
constexpr int LV_OBJ_FLAG_HIDDEN=1;
void lv_label_set_text(lv_obj_t *o,const char *text){o->text=text;}
void lv_arc_set_value(lv_obj_t *o,int n){o->progress=n;}
void lv_obj_set_width(lv_obj_t *o,int n){o->width=n;}
void lv_obj_add_flag(lv_obj_t *o,int){o->hidden=true;}
void lv_obj_remove_flag(lv_obj_t *o,int){o->hidden=false;}
namespace esphome { uint32_t now=1000;uint32_t millis(){return now;} }
namespace onx_controls { std::string feedback_text(const std::string &){return "";} }
STATE
ITEM
LOCAL
std::vector<TimerItem> all_timers(){return local_items();}
UPDATE
int main(){
 timers.synced_ms=1000;
 timers.local.push_back({"one","Tea",120,120,true});
 timer_screen_active=true;
 lv_obj_t label,arc,bar;
 timer_visuals.push_back({"local:one",&label,&arc,&bar,200,-1,""});
 const auto signature=timer_signature();tick_timers();
 assert(label.text=="02:00"&&arc.progress==1000&&bar.width==200);
 esphome::now+=1000;tick_timers();
 assert(timer_signature()==signature); // Countdown cannot recreate the button tree.
 assert(label.text=="01:59"&&arc.progress<1000&&bar.width<200);
 assert(timer_visuals[0].value==&label&&timer_visuals[0].arc==&arc);
 timers.local[0].active=false;timers.local[0].left=119;timers.synced_ms=esphome::now;
 assert(timer_signature()!=signature);
 tick_timers();const auto paused=label.text;esphome::now+=5000;tick_timers();assert(label.text==paused);
 // ESPHome invokes finished before erasing its timer: retire the mirrored
 // entry now so the ringing pseudo-item is the only item, with no Resume.
 timers.ringing=true;timers.ringing_name="Tea";timers.local[0].left=0;
 assert(local_items().size()==2);retire_local_timer("one");
 assert(local_items().size()==1&&local_items()[0].key=="local:ringing");
 timers.ringing=false;assert(local_items().empty());
 // Cancellation removes just its target and clears its pending action.
 timers.local={{"one","Tea",120,10,false},{"two","Pasta",300,30,true}};
 timers.message_key="local:one";timers.message="Stopping...";timers.command_pending=true;
 retire_local_timer("one");assert(timers.local.size()==1&&timers.local[0].id=="two");
 assert(timers.message.empty()&&!timers.command_pending);
 retire_local_timer("two");assert(local_items().empty());
}
'''.replace('STATE', state).replace('ITEM', item).replace('LOCAL', local).replace('UPDATE', update)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'timer.cpp'
            exe = Path(directory) / 'timer'
            cpp.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-I', str(root), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
