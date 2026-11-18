#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace onx_config {
// Step in page order. Slot numbers identify pages but are not their positions.
inline int step_carousel(const std::vector<int> &order,int current,int settings,int direction) {
  if(order.empty())return settings;
  if(current==settings)return direction<0?order.back():settings;
  const auto at=std::find(order.begin(),order.end(),current);
  if(at==order.end())return order.front();
  const int next=static_cast<int>(at-order.begin())+(direction<0?-1:1);
  if(next<0)return order.front();
  return next>=static_cast<int>(order.size())?settings:order[next];
}
inline bool integer_in_range(const std::string &value, unsigned minimum, unsigned maximum, unsigned &result) {
  if (value.empty() || value.size() > 4) return false;
  unsigned parsed = 0;
  for (char ch : value) {
    if (ch < '0' || ch > '9') return false;
    parsed = parsed * 10 + (ch - '0');
  }
  if (parsed < minimum || parsed > maximum) return false;
  result = parsed;
  return true;
}
inline bool valid_time(const std::string &value) {
  return value.size() == 5 && value[2] == ':' &&
         value[0] >= '0' && value[0] <= '2' && value[1] >= '0' && value[1] <= '9' &&
         value[3] >= '0' && value[3] <= '5' && value[4] >= '0' && value[4] <= '9' &&
         (value[0] - '0') * 10 + value[1] - '0' < 24;
}
inline bool valid_entity(const std::string &id) {
  const auto dot = id.find('.');
  if (id.size() > 128 || dot == std::string::npos || dot == 0 || dot + 1 == id.size() || id.find('.', dot + 1) != std::string::npos) return false;
  for (char ch : id) if (!(ch == '.' || ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) return false;
  return true;
}
inline bool valid_action(const std::string &id) {
  return id.empty() || id == "none" || (valid_entity(id) && (id.rfind("script.", 0) == 0 || id.rfind("automation.", 0) == 0));
}
inline bool valid_press_action(const std::string &entity,const std::string &action) {
  return valid_action(action) || (action=="open_playlists" && !entity.empty());
}
inline bool binding_matches(const std::string &bound, const std::string &current) {
  return !current.empty() && bound == current;
}
}  // namespace onx_config
