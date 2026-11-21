#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <cstring>
#include <initializer_list>
#include "onx_config_rules.h"

// Page membership refers to existing configured entities, so their shared HA
// subscriptions, availability handling and control state remain authoritative.
namespace onx_panel_layout {
enum class Kind : uint8_t { SINGLE, TWO_CONTROLS, CONTROL_SENSORS, ACTION_GRID, STACK };
constexpr int MAX_GRID_AXIS=4, MAX_CELLS=MAX_GRID_AXIS*MAX_GRID_AXIS;
// Stack pages: a column of up to six rows, each a full card, a split pair, a
// compact row or a strip of three chips. The row list is packed base 5 into the
// page's rows/cols bytes (row i is digit i; 0 ends the list), so stored pages
// keep their format.
enum class Row : uint8_t { NONE, FULL, SPLIT, COMPACT, CHIPS };
constexpr int MAX_ROWS=6, CHIPS_PER_ROW=3;
inline int row_cells(Row r) { return r==Row::SPLIT?2:r==Row::CHIPS?CHIPS_PER_ROW:r==Row::NONE?0:1; }
inline Row stack_row(uint16_t code,int i) { for(int k=0;k<i;++k)code/=5; return Row(code%5); }
inline int stack_rows(uint16_t code) { int n=0; while(n<MAX_ROWS && stack_row(code,n)!=Row::NONE)++n; return n; }
inline int stack_cells(uint16_t code) { int n=0; for(int i=0;i<stack_rows(code);++i)n+=row_cells(stack_row(code,i)); return n; }
inline uint16_t stack_code(std::initializer_list<Row> rows) {
  uint32_t code=0,m=1; for(Row r:rows){code+=uint32_t(r)*m;m*=5;} return uint16_t(code);
}
inline bool valid_stack(uint16_t code) {
  if(code>=15625)return false;  // 5^6: at most six rows.
  const int n=stack_rows(code);
  if(n==0)return false;
  for(int i=n;i<MAX_ROWS;++i)if(stack_row(code,i)!=Row::NONE)return false;
  return stack_cells(code)<=MAX_CELLS;
}
inline const char *row_name(Row r) { return r==Row::SPLIT?"split":r==Row::COMPACT?"compact":r==Row::CHIPS?"chips":"full"; }
inline bool parse_row(const std::string &s,Row &r) {
  if(s=="full")r=Row::FULL; else if(s=="split")r=Row::SPLIT; else if(s=="compact")r=Row::COMPACT; else if(s=="chips")r=Row::CHIPS;
  else return false;
  return true;
}
// The row a stack position belongs to (Row::NONE past the end).
inline Row stack_row_of(uint16_t code,int pos) {
  for(int i=0;i<stack_rows(code);++i){const Row r=stack_row(code,i);if(pos<row_cells(r))return r;pos-=row_cells(r);}
  return Row::NONE;
}
inline bool grid_axis(int n) { return n>=1 && n<=MAX_GRID_AXIS; }
struct Page {
  uint32_t magic{0x52435031};
  char primary[129]{}, members[3][129]{};
  Kind kind{Kind::SINGLE};
};
inline const char *name(Kind k) { return k==Kind::TWO_CONTROLS?"two_controls":k==Kind::CONTROL_SENSORS?"control_sensors":k==Kind::ACTION_GRID?"action_grid":k==Kind::STACK?"stack":"single"; }
inline bool parse(const std::string &s, Kind &k) {
  if(s=="single")k=Kind::SINGLE;
  else if(s=="two_controls")k=Kind::TWO_CONTROLS;
  else if(s=="control_sensors")k=Kind::CONTROL_SENSORS;
  else if(s=="action_grid")k=Kind::ACTION_GRID;
  else if(s=="stack")k=Kind::STACK;
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
// Stack geometry. Chip strips and compact rows have a fixed height; full and
// split rows share the rest. Without full/split rows the compact rows grow (up
// to a limit) and the stack starts at the top. `filled` marks occupied
// positions: chips spread over the occupied ones; empty chips get no area.
inline std::array<Rect,MAX_CELLS> stack_geometry(uint16_t code,int w,int h,bool detail=false,uint32_t filled=0xFFFF) {
  const int top=detail?DETAIL_TOP:MAIN_TOP,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom,inner=w-pad*2;
  const bool small=std::min(w,h)<320;
  const int chip_h=small?32:42,row_h=small?42:56,row_max=small?56:76;
  std::array<Rect,MAX_CELLS> out{};
  const int n=stack_rows(code);
  if(n==0)return out;
  int flex=0,chips=0,rows=0;
  for(int i=0;i<n;++i){const Row r=stack_row(code,i);if(r==Row::FULL||r==Row::SPLIT)++flex;else if(r==Row::CHIPS)++chips;else ++rows;}
  const int free=body-gap*(n-1);
  int ch=chip_h,rh=row_h,fh=0;
  if(flex) {
    fh=(free-chips*ch-rows*rh)/flex;
    const int min_flex=small?72:96;
    if(fh<min_flex && rows+chips) {  // Shrink the fixed rows before the cards.
      const int fixed=std::max(free/4,free-flex*min_flex);
      const int want=chips*ch+rows*rh;
      ch=ch*fixed/want;rh=rh*fixed/want;fh=(free-chips*ch-rows*rh)/flex;
    }
  } else if(rows) {
    rh=std::min(row_max,(free-chips*ch)/rows);
  } else {
    ch=std::min(ch,free/chips);
  }
  if(!flex && rows+chips && chips*ch+rows*rh>free) {
    const int want=chips*ch+rows*rh;ch=ch*free/want;rh=rh*free/want;
  }
  int y=top,pos=0,flex_seen=0;
  for(int i=0;i<n;++i) {
    const Row r=stack_row(code,i);
    int rhh=r==Row::CHIPS?ch:r==Row::COMPACT?rh:fh;
    if((r==Row::FULL||r==Row::SPLIT) && ++flex_seen==flex)rhh=fh+free-chips*ch-rows*rh-flex*fh;  // The last card takes the rounding.
    if(r==Row::SPLIT) {
      const int half=(inner-gap)/2;
      out[pos]={pad,y,half,rhh};out[pos+1]={pad+half+gap,y,inner-half-gap,rhh};
    } else if(r==Row::CHIPS) {
      int count=0;for(int k=0;k<CHIPS_PER_ROW;++k)if(filled&(1u<<(pos+k)))++count;
      const int cw=count?(inner-gap*(count-1))/count:0;int x=pad,seen=0;
      for(int k=0;k<CHIPS_PER_ROW;++k) {
        if(!(filled&(1u<<(pos+k)))){out[pos+k]={x,y,0,0};continue;}
        const int ww=++seen==count?pad+inner-x:cw;
        out[pos+k]={x,y,ww,rhh};x+=ww+gap;
      }
    } else out[pos]={pad,y,inner,rhh};
    pos+=row_cells(r);y+=rhh+gap;
  }
  return out;
}
inline std::array<Rect,MAX_CELLS> geometry(Kind k,int w,int h,bool detail=false,int rows=2,int cols=2) {
  if(k==Kind::STACK)return stack_geometry(uint16_t(rows|(cols<<8)),w,h,detail);
  // The main carousel keeps a slim row for the notification bell and the
  // bottom dock; details use the whole screen (pull down for notifications).
  const int top=detail?DETAIL_TOP:MAIN_TOP,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom,inner=w-pad*2;
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
