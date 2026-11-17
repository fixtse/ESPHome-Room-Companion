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
    // Landscape (the same panel turned 90 degrees): groups side by side.
    const int lw=h,lh=w;
    for(auto kind:{Kind::SINGLE,Kind::TWO_CONTROLS,Kind::CONTROL_SENSORS}) {
      auto boxes=geometry(kind,lw,lh);
      const int count=kind==Kind::SINGLE?1:kind==Kind::TWO_CONTROLS?2:4;
      for(int i=0;i<count;++i) {
        const auto r=boxes[i];
        assert(r.x>=12 && r.x+r.w<=lw-12 && r.y>=MAIN_TOP && r.y+r.h<=lh-52 && r.w>0 && r.h>0);
        for(int j=0;j<i;++j) {  // No overlaps.
          const auto o=boxes[j];
          assert(r.x>=o.x+o.w || o.x>=r.x+r.w || r.y>=o.y+o.h || o.y>=r.y+r.h);
        }
      }
      if(kind==Kind::TWO_CONTROLS)assert(boxes[0].y==boxes[1].y && boxes[1].x+boxes[1].w==lw-12);
      if(kind==Kind::CONTROL_SENSORS)assert(boxes[3].y+boxes[3].h==lh-52 && boxes[1].x>boxes[0].x+boxes[0].w);
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
