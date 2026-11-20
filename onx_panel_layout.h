#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <cstring>
#include "onx_config_rules.h"

// Page membership refers to existing configured entities, so their shared HA
// subscriptions, availability handling and control state remain authoritative.
namespace onx_panel_layout {
enum class Kind : uint8_t { SINGLE, TWO_CONTROLS, CONTROL_SENSORS, ACTION_GRID };
constexpr int MAX_GRID_AXIS=4, MAX_CELLS=MAX_GRID_AXIS*MAX_GRID_AXIS;
inline bool grid_axis(int n) { return n>=1 && n<=MAX_GRID_AXIS; }
struct Page {
  uint32_t magic{0x52435031};
  char primary[129]{}, members[3][129]{};
  Kind kind{Kind::SINGLE};
};
inline const char *name(Kind k) { return k==Kind::TWO_CONTROLS?"two_controls":k==Kind::CONTROL_SENSORS?"control_sensors":k==Kind::ACTION_GRID?"action_grid":"single"; }
inline bool parse(const std::string &s, Kind &k) {
  if(s=="single")k=Kind::SINGLE;
  else if(s=="two_controls")k=Kind::TWO_CONTROLS;
  else if(s=="control_sensors")k=Kind::CONTROL_SENSORS;
  else if(s=="action_grid")k=Kind::ACTION_GRID;
  else return false;
  return true;
}
inline bool sensor(const std::string &s) { return s.rfind("sensor.",0)==0 || s.rfind("binary_sensor.",0)==0; }
inline bool valid(const Page &p) {
  if(p.magic!=0x52435031 || !onx_config::valid_entity(p.primary) || static_cast<unsigned>(p.kind)>2)return false;
  if(p.kind!=Kind::SINGLE && sensor(p.primary))return false;
  if(p.kind==Kind::TWO_CONTROLS && sensor(p.members[0]))return false;
  const int count=p.kind==Kind::TWO_CONTROLS?1:p.kind==Kind::CONTROL_SENSORS?3:0;
  for(int i=0;i<count;++i) {
    if(!onx_config::valid_entity(p.members[i]) || std::strcmp(p.primary,p.members[i])==0)return false;
    if(p.kind==Kind::CONTROL_SENSORS && !sensor(p.members[i]))return false;
    for(int j=0;j<i;++j)if(std::strcmp(p.members[i],p.members[j])==0)return false;
  }
  return true;
}
struct Rect {int x,y,w,h;};
constexpr int MAIN_TOP=40,DETAIL_TOP=12;
inline std::array<Rect,MAX_CELLS> geometry(Kind k,int w,int h,bool detail=false,int rows=2,int cols=2) {
  // The main carousel keeps a slim row for the notification bell and the
  // bottom dock; details use the whole screen (pull down for notifications).
  const int top=detail?DETAIL_TOP:MAIN_TOP,bottom=detail?12:std::min(w,h)==240?78:52,gap=8,pad=12,body=h-top-bottom,inner=w-pad*2;
  std::array<Rect,MAX_CELLS> out{};
  if(k==Kind::ACTION_GRID && grid_axis(rows) && grid_axis(cols)) {
    // Grids keep a little room below the status row and above the dock.
    constexpr int GRID_TOP=6,GRID_BOTTOM=4;
    const int usable_w=w-16-gap*(cols-1), usable_h=body-GRID_TOP-GRID_BOTTOM-gap*(rows-1);
    for(int i=0;i<rows*cols;++i) {
      const int c=i%cols,r=i/cols;
      out[i]={8+c*usable_w/cols+c*gap,top+GRID_TOP+r*usable_h/rows+r*gap,
              (c+1)*usable_w/cols-c*usable_w/cols,(r+1)*usable_h/rows-r*usable_h/rows};
    }
    return out;
  }
  out[0]={pad,top,inner,body};
  if(w>h) {
    // Landscape: groups sit side by side; sensors form a column on the right.
    if(k==Kind::TWO_CONTROLS) {
      const int half=(inner-gap)/2;out[0].w=half;out[1]={pad+half+gap,top,inner-half-gap,body};
    } else if(k==Kind::CONTROL_SENSORS) {
      const int main=(inner-gap)*58/100,row=(body-gap*2)/3,x=pad+main+gap;
      out[0].w=main;
      for(int i=0;i<3;++i)out[i+1]={x,top+i*(row+gap),inner-main-gap,i==2?body-2*(row+gap):row};
    }
    return out;
  }
  if(k==Kind::TWO_CONTROLS) {
    const int half=(body-gap)/2;out[0].h=half;out[1]={pad,top+half+gap,inner,body-half-gap};
  } else if(k==Kind::CONTROL_SENSORS) {
    const int main=(body-gap)*55/100,row=(body-main-gap*3)/3;
    out[0].h=main;
    for(int i=0;i<3;++i)out[i+1]={pad,top+main+gap+i*(row+gap),inner,row};
  }
  return out;
}
}
