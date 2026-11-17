#include <cassert>
#include <cstring>
#include "../onx_panel_layout.h"
using namespace onx_panel_layout;
int main() {
  for(auto [w,h]:{std::pair{240,320},std::pair{320,480}}) {
    for(auto kind:{Kind::SINGLE,Kind::TWO_CONTROLS,Kind::CONTROL_SENSORS}) {
      auto boxes=geometry(kind,w,h);
      const int count=kind==Kind::SINGLE?1:kind==Kind::TWO_CONTROLS?2:4;
      int bottom=MAIN_TOP;
      for(int i=0;i<count;++i) {
        const auto r=boxes[i];assert(r.w==w-24 && r.x==12 && r.h>0);
        assert(r.y>=bottom && r.y+r.h<=h-52);bottom=r.y+r.h;
      }
      assert(bottom>=h-55); // Every composition fills the rectangle above the dock.
    }
    auto detail=geometry(Kind::SINGLE,w,h,true)[0];assert(detail.y==DETAIL_TOP && detail.y+detail.h==h-12);
    assert(geometry(Kind::SINGLE,w,h)[0].y==MAIN_TOP); // Bell row only, no dashboard header.
  }
  Page p;std::strcpy(p.primary,"light.kitchen");assert(valid(p));
  p.kind=Kind::TWO_CONTROLS;assert(!valid(p));
  std::strcpy(p.members[0],"switch.fan");assert(valid(p));
  std::strcpy(p.members[0],p.primary);assert(!valid(p));
  std::strcpy(p.members[0],"sensor.temperature");assert(!valid(p));
  p.kind=Kind::CONTROL_SENSORS;assert(!valid(p));
  std::strcpy(p.members[1],"sensor.humidity");std::strcpy(p.members[2],"binary_sensor.door");assert(valid(p));
  std::strcpy(p.members[2],p.members[1]);assert(!valid(p));
  std::strcpy(p.members[2],"light.other");assert(!valid(p));
  Kind kind;assert(!parse("bogus",kind));assert(parse("two_controls",kind)&&kind==Kind::TWO_CONTROLS);
}
