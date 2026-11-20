#include "../onx_cover_presets.h"
#include <cassert>
int main() {
 using namespace onx_cover_presets;
 const auto defaults_config=defaults();assert(defaults_config.enabled&&encode(defaults_config)=="15,25,50,75");
 Config c;assert(parse("15, 25,50,75",c)&&c.count==4&&encode(c)=="15,25,50,75");
 assert(parse("",c)&&c.count==0);
 assert(parse("0,100",c)&&c.count==2);
 for(const auto *invalid:{"-1","101","25,25","10,20,30,40,50","1.5","15,","10 20","10,,20"})assert(!parse(invalid,c));
 assert(parse("20,40,60,80",c));c.enabled=false;assert(encode(c)=="20,40,60,80"&&c.count==4); // Toggle preserves values.
}
