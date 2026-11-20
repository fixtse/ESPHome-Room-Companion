cmake_minimum_required(VERSION 3.20)
project(room_companion_emulator C CXX)
# Room Companion panels on esphome-lvgl-emulator. Configure from the
# repository root:  cmake -S emulator -B emulator/build
# To use a local emulator checkout instead of GitHub:
#   -DFETCHCONTENT_SOURCE_DIR_ESPHOME_LVGL_EMULATOR=/path/to/esphome-lvgl-emulator
include(FetchContent)
FetchContent_Declare(esphome_lvgl_emulator
  GIT_REPOSITORY https://github.com/fixtse/esphome-lvgl-emulator.git GIT_TAG main GIT_SHALLOW TRUE)
# Build LVGL exactly as the firmware's ESPHome 2026.11.0 configuration does.
set(LVGL_EMU_LVGL_VERSION "v9.6.0" CACHE STRING "" FORCE)
set(LVGL_EMU_LV_CONF "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "" FORCE)
FetchContent_MakeAvailable(esphome_lvgl_emulator)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_lvgl_emulator_app(rc-emulator
  SOURCES app.cpp ${REPO}/my_components/onx_storage/onx_storage.cpp
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO}
  DEFINITIONS USE_ROOM_COMPANION=1)
