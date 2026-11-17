#include "../onx_sensor_view.h"
#include <cassert>
#include <cstdlib>
int main() {
  using namespace onx_sensor_view;
  setenv("TZ","UTC",1);tzset();
  Config c;snprintf(c.primary,sizeof(c.primary),"sensor.remaining");
  snprintf(c.secondary,sizeof(c.secondary),"sensor.reset");
  snprintf(c.text,sizeof(c.text),"Resets {relative} ({time})");
  State s;s.bind(c);
  assert(format(c,s,1735689600)=="Waiting for secondary data");
  s.received=true;assert(format(c,s,1735689600)=="Secondary unavailable");
  s.available=true;s.value="2025-01-01T03:00:00Z";s.secondary_epoch=1735700400;
  onx_display_settings::datetime.hour_format=1;
  assert(format(c,s,1735689600)=="Resets in 3 hours (3:00 AM)");
  onx_display_settings::datetime.show_ampm=0;
  assert(format(c,s,1735689600)=="Resets in 3 hours (3:00)");
  snprintf(c.text,sizeof(c.text),"{value} {unit}");s.value="Ready";s.unit="";
  assert(format(c,s,1735689600)=="Ready ");
  s.sample(1735689600,10);s.sample(1735689610,20);s.sample(1735689605,1);
  auto history=s.history(1735689620);assert(history.size()==1&&history[0].value==20);
  s.sample(1735690800,30);assert(s.history(1735690800).size()==2);
  s.sample(1735690801,NAN);assert(s.history(1735690801).size()==2);
  assert(s.history(1735734000).size()==1);
  State ranged;
  ranged.range_seconds=604800;
  ranged.sample(1735689600,10);
  ranged.sample(1735689600+3*86400,20);
  assert(ranged.history(1735689600+6*86400).size()==2);
  assert(ranged.history(1735689600+8*86400).size()==1);
  ranged.range_seconds=3600;ranged.points={};
  ranged.sample(1735689600,10);ranged.sample(1735693200,20);
  assert(ranged.history(1735693201).size()==1);
  Config other=c;snprintf(other.secondary,sizeof(other.secondary),"sensor.other");s.bind(other);
  assert(!s.received&&s.history(1735690801).empty());
}
