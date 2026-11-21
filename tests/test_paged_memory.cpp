#include "../onx_gesture.h"
#include "../onx_icon_state.h"
#include "../onx_memory.h"
#include "../onx_playlist.h"
#include "../onx_sensor_view.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  static_assert(onx_memory::ENTRY_COUNT == 250, "Expected 250 entry ceiling");

  // Lazy PagedArray allocation, const reads without allocation, pointer stability, and fill.
  onx_memory::PagedArray<int, onx_memory::ENTRY_COUNT, 16> arr;
  assert(arr.size() == 250);
  assert(arr.allocated_pages() == 0);
  assert(arr.allocated_limit() == 0);
  assert(!arr.has(0) && !arr.has(199) && !arr.has(250));
  assert(arr.find(0) == nullptr);

  const auto &carr = arr;
  assert(carr[0] == 0 && carr[249] == 0);
  assert(arr.allocated_pages() == 0 && arr.allocated_limit() == 0);

  arr[3] = 42;
  int *ptr3 = &arr[3];
  assert(arr.allocated_pages() == 1);
  assert(arr.allocated_limit() == 16);
  assert(arr.has(3) && arr.has(15) && !arr.has(16));
  assert(arr.find(3) == ptr3 && arr.find(16) == nullptr);

  // Access a high slot (> 40 and > 64) without allocating intermediate pages.
  arr[249] = 99;
  assert(arr.allocated_pages() == 2);
  assert(arr.allocated_limit() == 250);
  assert(arr.has(249) && !arr.has(100));
  assert(&arr[3] == ptr3 && *ptr3 == 42);

  // fill() updates only allocated pages and does not materialize untouched pages.
  arr.fill(7);
  assert(arr.allocated_pages() == 2);
  assert(arr[3] == 7 && arr[249] == 7);
  assert(carr[100] == 0 && !arr.has(100));

  // Sparse range-for iterates allocated pages only (16 on page 0 + 10 on partial page 15 = 26).
  size_t visited = 0;
  for (int v : carr) {
    assert(v == 7);
    ++visited;
  }
  assert(visited == 26);
  assert(arr.allocated_pages() == 2);

  // Verify high-slot lookups across gesture, playlist, icon, and sensor modules.
  constexpr size_t hi = 199;
  std::snprintf(onx_gesture::configs[hi].entity, sizeof(onx_gesture::configs[hi].entity), "light.loft_%zu", hi);
  std::snprintf(onx_gesture::configs[hi].actions[0], sizeof(onx_gesture::configs[hi].actions[0]), "script.bright");
  assert(onx_gesture::routes(int(hi), 0));
  assert(!onx_gesture::configs.has(50));

  std::snprintf(onx_playlist::configs[hi].entity, sizeof(onx_playlist::configs[hi].entity), "media_player.studio");
  onx_playlist::configs[hi].count = 1;
  assert(onx_playlist::configs.has(hi) && !onx_playlist::configs.has(50));

  onx_icon_state::metadata[hi].bind("switch.bench");
  onx_icon_state::metadata[hi].configured = "mdi:power-socket";
  assert(onx_icon_state::selected_icon("switch.bench") == "mdi:power-socket");
  assert(!onx_icon_state::metadata.has(50));

  std::snprintf(onx_sensor_view::configs[hi].primary, sizeof(onx_sensor_view::configs[hi].primary), "sensor.loft_temp");
  assert(onx_sensor_view::index("sensor.loft_temp") == int(hi));
  assert(!onx_sensor_view::configs.has(50));

  return 0;
}
