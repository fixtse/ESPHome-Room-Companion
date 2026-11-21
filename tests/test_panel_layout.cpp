#include <cassert>
#include <cstring>
#include "../onx_panel_layout.h"
using namespace onx_panel_layout;
int main() {
  // USB and grid may be backed by nonconsecutive slots, in any page order.
  const std::vector<int> pages{4,0,2};
  assert(onx_config::step_carousel(pages,4,6,1)==0);
  assert(onx_config::step_carousel(pages,0,6,1)==2);
  assert(onx_config::step_carousel(pages,2,6,1)==6);
  assert(onx_config::step_carousel(pages,6,6,-1)==2);
  assert(onx_config::step_carousel(pages,2,6,-1)==0);
  assert(onx_config::step_carousel(pages,0,6,-1)==4);
  assert(onx_config::step_carousel(pages,4,6,-1)==4);
  for(auto [w,h]:{std::pair{240,320},std::pair{320,480}}) {
    const int dock=52;
    for(auto kind:{Kind::SINGLE,Kind::TWO_CONTROLS,Kind::CONTROL_SENSORS}) {
      auto boxes=geometry(kind,w,h);
      const int count=kind==Kind::SINGLE?1:kind==Kind::TWO_CONTROLS?2:4;
      int bottom=MAIN_TOP;
      for(int i=0;i<count;++i) {
        const auto r=boxes[i];assert(r.w==w-24 && r.x==12 && r.h>0);
        assert(r.y>=bottom && r.y+r.h<=h-dock);bottom=r.y+r.h;
      }
      assert(bottom>=h-dock-3); // Every composition fills the rectangle above the dock.
    }
    // Landscape (the same panel turned 90 degrees): groups side by side.
    const int lw=h,lh=w;
    for(auto kind:{Kind::SINGLE,Kind::TWO_CONTROLS,Kind::CONTROL_SENSORS}) {
      auto boxes=geometry(kind,lw,lh);
      const int count=kind==Kind::SINGLE?1:kind==Kind::TWO_CONTROLS?2:4;
      for(int i=0;i<count;++i) {
        const auto r=boxes[i];
        assert(r.x>=12 && r.x+r.w<=lw-12 && r.y>=MAIN_TOP && r.y+r.h<=lh-dock && r.w>0 && r.h>0);
        for(int j=0;j<i;++j) {  // No overlaps.
          const auto o=boxes[j];
          assert(r.x>=o.x+o.w || o.x>=r.x+r.w || r.y>=o.y+o.h || o.y>=r.y+r.h);
        }
      }
      if(kind==Kind::TWO_CONTROLS)assert(boxes[0].y==boxes[1].y && boxes[1].x+boxes[1].w==lw-12);
      if(kind==Kind::CONTROL_SENSORS)assert(boxes[3].y+boxes[3].h==lh-dock && boxes[1].x>boxes[0].x+boxes[0].w);
    }
    auto detail=geometry(Kind::SINGLE,w,h,true)[0];assert(detail.y==DETAIL_TOP && detail.y+detail.h==h-12);
    assert(geometry(Kind::SINGLE,w,h)[0].y==MAIN_TOP); // Bell row only, no dashboard header.
    for(auto [width,height]:{std::pair{w,h},std::pair{h,w}})
      for(int rows=1;rows<=4;++rows)for(int cols=1;cols<=4;++cols) {
        const auto keys=geometry(Kind::ACTION_GRID,width,height,false,rows,cols);
        for(int i=0;i<rows*cols;++i) {
          const auto r=keys[i];
          assert(r.w>0 && r.h>0 && r.x>=8 && r.x+r.w<=width-8);
          assert(r.y>=MAIN_TOP+6 && r.y+r.h<=height-dock-4);  // Room under the status row and above the dock.
          if(i%cols)assert(r.x==keys[i-1].x+keys[i-1].w+8);
          if(i>=cols)assert(r.y==keys[i-cols].y+keys[i-cols].h+8);
        }
      }
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
  assert(parse("stack",kind)&&kind==Kind::STACK && std::strcmp(name(Kind::STACK),"stack")==0);
  // Stacks: packed rows, cell counts and limits.
  const auto board=stack_code({Row::CHIPS,Row::FULL,Row::SPLIT});
  assert(stack_rows(board)==3 && stack_cells(board)==6 && valid_stack(board));
  assert(stack_row_of(board,2)==Row::CHIPS && stack_row_of(board,3)==Row::FULL && stack_row_of(board,5)==Row::SPLIT && stack_row_of(board,6)==Row::NONE);
  assert(!valid_stack(0) && !valid_stack(15625));
  assert(!valid_stack(stack_code({Row::CHIPS,Row::CHIPS,Row::CHIPS,Row::CHIPS,Row::CHIPS,Row::CHIPS})));  // 18 cells.
  assert(valid_stack(stack_code({Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT})));
  for(auto [w,h]:{std::pair{240,320},std::pair{320,480},std::pair{480,320},std::pair{320,240}}) {
    const int dock=52;
    for(auto code:{board,stack_code({Row::FULL}),stack_code({Row::CHIPS,Row::FULL}),stack_code({Row::FULL,Row::COMPACT,Row::COMPACT,Row::COMPACT}),
                   stack_code({Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT,Row::COMPACT}),
                   stack_code({Row::CHIPS,Row::CHIPS,Row::SPLIT,Row::SPLIT,Row::COMPACT,Row::FULL}),stack_code({Row::CHIPS})}) {
      const auto boxes=stack_geometry(code,w,h);const int n=stack_cells(code);
      for(int i=0;i<n;++i) {
        const auto r=boxes[i];
        assert(r.w>0 && r.h>0 && r.x>=12 && r.x+r.w<=w-12 && r.y>=MAIN_TOP && r.y+r.h<=h-dock);
        for(int j=0;j<i;++j){const auto o=boxes[j];assert(r.x>=o.x+o.w || o.x>=r.x+r.w || r.y>=o.y+o.h || o.y>=r.y+r.h);}
      }
      bool flex=false;for(int i=0;i<stack_rows(code);++i)flex|=stack_row(code,i)==Row::FULL||stack_row(code,i)==Row::SPLIT;
      if(flex)assert(boxes[n-1].y+boxes[n-1].h==h-dock || stack_row(code,stack_rows(code)-1)!=Row::FULL);
    }
    // Chips spread over the filled ones; empty chips get no area.
    const auto spread=stack_geometry(board,w,h,false,0b111010);
    assert(spread[0].w==0 && spread[2].w==0 && spread[1].x==12 && spread[1].x+spread[1].w==w-12);
  }
}
