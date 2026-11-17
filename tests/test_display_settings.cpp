#include "../onx_display_settings.h"
#include <cassert>
#include <utility>
int main() {
  using namespace onx_display_settings;
  uint8_t format=255;
  assert(parse_date_format("dd-mm",format)&&format==0);
  assert(format_iso_date("2026-12-31T12:00:00Z")=="31-12");
  datetime.date_format=1;assert(format_iso_date("2026-12-31")=="12-31");
  datetime.date_format=2;assert(format_iso_date("2026-12-31")=="2026-12-31");
  assert(format_iso_date("unknown")=="unknown");
  for(const auto &pair : {std::pair<const char *,const char *>{"dd-mmm","10 Nov"},
                         {"mmm-dd","Nov 10"},{"yyyy-mmm-dd","2026-Nov-10"}}) {
    assert(parse_date_format(pair.first,datetime.date_format));
    assert(date_format_text(datetime.date_format)==pair.first);
    assert(format_iso_date("2026-11-10T23:30:00Z")==pair.second);
  }
  assert(format_iso_date("2026-00-10")=="2026-00-10");
  assert(format_iso_date("2026-13-10")=="2026-13-10");
  assert(!parse_date_format("bad",format));
  assert(mix(0xFFFFFF,0x000000,100)==0xFFFFFF && mix(0xFFFFFF,0x000000,0)==0);
  assert(mix(0x62B0FF,0x000000,80)==0x4E8DCC);
  current.media_color=0xF97316;assert(accent()==0xF97316 && accent_strong()!=accent() && accent_soft()!=accent());
  current.media_color=DEFAULT_ACCENT;
  assert(parse_hour_format("12",format)&&format==1);
  datetime.hour_format=format;assert(std::string(time_pattern())=="%I:%M %p");
  assert(parse_hour_format("24",format)&&format==0);
  assert(!parse_hour_format("13",format));
  uint32_t color=0;
  assert(parse_color("#62b0ff",color)&&color==0x62B0FF&&color_text(color)=="#62B0FF");
  for(const auto &bad : {"", "red", "#fff", "#0000000", "#12zz00", "123456"})assert(!parse_color(bad,color));
  uint8_t scope=255;
  assert(parse_scope("main",scope)&&scope==0);
  assert(standby_allowed(scope,false)&&!standby_allowed(scope,true));
  assert(parse_scope("all",scope)&&standby_allowed(scope,false)&&standby_allowed(scope,true));
  assert(parse_scope("never",scope)&&!standby_allowed(scope,false)&&!standby_allowed(scope,true));
  assert(!parse_scope("invalid",scope));
  assert(current.standby_scope==0&&current.media_color==0x62B0FF);
}
