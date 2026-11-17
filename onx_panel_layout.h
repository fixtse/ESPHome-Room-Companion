#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <cstring>
#include "onx_config_rules.h"

// Page membership refers to existing configured entities, so their shared HA
// subscriptions, availability handling and control state remain authoritative.
namespace onx_panel_layout {
enum class Kind : uint8_t { SINGLE, TWO_CONTROLS, CONTROL_SENSORS };
struct Page {
  uint32_t magic{0x52435031};
  char primary[129]{}, members[3][129]{};
  Kind kind{Kind::SINGLE};
};
inline const char *name(Kind k) { return k==Kind::TWO_CONTROLS?"two_controls":k==Kind::CONTROL_SENSORS?"control_sensors":"single"; }
inline bool parse(const std::string &s, Kind &k) {
  if(s=="single")k=Kind::SINGLE;
  else if(s=="two_controls")k=Kind::TWO_CONTROLS;
  else if(s=="control_sensors")k=Kind::CONTROL_SENSORS;
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
inline std::array<Rect,4> geometry(Kind k,int w,int h,bool detail=false) {
  // Header for dashboard/bell; bottom dock only on the carousel.
  const int top=56,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom;
  std::array<Rect,4> out{};
  out[0]={pad,top,w-pad*2,body};
  if(k==Kind::TWO_CONTROLS) {
    const int half=(body-gap)/2;out[0].h=half;out[1]={pad,top+half+gap,w-pad*2,body-half-gap};
  } else if(k==Kind::CONTROL_SENSORS) {
    const int main=(body-gap)*55/100,row=(body-main-gap*3)/3;
    out[0].h=main;
    for(int i=0;i<3;++i)out[i+1]={pad,top+main+gap+i*(row+gap),w-pad*2,row};
  }
  return out;
}
}
