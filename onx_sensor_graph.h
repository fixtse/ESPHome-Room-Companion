#pragma once
#include "onx_sensor_view.h"
#include "onx_geom.h"
#ifdef USE_LVGL
namespace onx_sensor_graph {
inline lv_obj_t *canvas=nullptr,*minimum_label=nullptr,*maximum_label=nullptr;
inline int plot_left=42,plot_right=185,plot_bottom=64;
struct Pixel {int16_t x,y;uint32_t epoch;};
inline std::array<Pixel,73> pixels;
inline size_t count=0;
inline uint32_t range_seconds=43200;
inline uint32_t ink=0x62B0FF;
// Set while the rectangular detail screen draws the graph. The base layout's
// periodic detail refresh must not hide, re-parent or resize the shared canvas
// underneath it (that made the graph jump between two sizes every second).
inline bool panel_owned=false;
inline void draw(lv_event_t *event) {
  lv_area_t area;lv_obj_get_coords(canvas,&area);
  auto *layer=lv_event_get_layer(event);
  lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.width=1;line.color=lv_color_hex(0x344653);
  using onx_geom::S;
  for(int y : {0,plot_bottom/2,plot_bottom}) {
    line.p1.x=area.x1+plot_left;line.p1.y=area.y1+y;line.p2.x=area.x1+plot_right;line.p2.y=area.y1+y;
    lv_draw_line(layer,&line);
  }
  line.width=2;line.color=lv_color_hex(ink);
  for(size_t i=1;i<count;++i) {
    if(pixels[i].epoch-pixels[i-1].epoch>std::max<uint32_t>(1200,range_seconds/24))continue;
    line.p1.x=area.x1+pixels[i-1].x;line.p1.y=area.y1+pixels[i-1].y;
    line.p2.x=area.x1+pixels[i].x;line.p2.y=area.y1+pixels[i].y;
    lv_draw_line(layer,&line);
  }
}
inline void hide() {if(canvas)lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);}
inline void render(lv_obj_t *parent,const std::string &entity,uint32_t now,uint32_t color,const lv_font_t *font,int width=0,int height=0) {
  const int i=onx_sensor_view::index(entity);
  if(i<0||!onx_sensor_view::configs[i].graph){hide();return;}
  if(!canvas) {
    canvas=lv_obj_create(parent);lv_obj_set_size(canvas,onx_geom::S(186),onx_geom::S(65));lv_obj_align(canvas,LV_ALIGN_TOP_MID,0,onx_geom::S(104));
    lv_obj_set_style_bg_opa(canvas,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(canvas,0,0);
    lv_obj_set_style_pad_all(canvas,0,0);lv_obj_clear_flag(canvas,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(canvas,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas,draw,LV_EVENT_DRAW_MAIN,nullptr);
    minimum_label=lv_label_create(canvas);maximum_label=lv_label_create(canvas);
    for(auto *label:{minimum_label,maximum_label}) {
      lv_obj_set_style_text_font(label,font,0);
      lv_obj_set_style_text_color(label,lv_color_hex(onx_display_settings::muted_text()),0);
      lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_RIGHT,0);
      lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    }
  }
  // The panel detail parks the shared canvas on lv_layer_top() when it lets
  // go of it; showing it there would draw it above overlays such as Assist.
  if(lv_obj_get_parent(canvas)!=parent){lv_obj_set_parent(canvas,parent);lv_obj_align(canvas,LV_ALIGN_TOP_MID,0,onx_geom::S(104));}
  lv_obj_set_size(canvas,width>0?width:onx_geom::S(186),height>0?height:onx_geom::S(65));
  lv_obj_clear_flag(canvas,LV_OBJ_FLAG_HIDDEN);ink=color;count=0;range_seconds=onx_sensor_view::states[i].range_seconds;
  plot_right=std::max(1,static_cast<int>(lv_obj_get_style_width(canvas,LV_PART_MAIN))-1);
  plot_bottom=std::max(1,static_cast<int>(lv_obj_get_style_height(canvas,LV_PART_MAIN))-1);
  const auto history=onx_sensor_view::states[i].history(now);
  for(auto *label:{minimum_label,maximum_label})lv_obj_set_style_text_color(label,lv_color_hex(onx_display_settings::muted_text()),0);
  lv_obj_add_flag(minimum_label,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(maximum_label,LV_OBJ_FLAG_HIDDEN);
  if(!history.empty()) {
    float low=history.front().value,high=low;
    for(const auto &p:history){low=std::min(low,p.value);high=std::max(high,p.value);}
    const auto labels=onx_sensor_view::axis_labels(low,high);
    lv_label_set_text(minimum_label,labels.first.c_str());
    lv_label_set_text(maximum_label,labels.second.c_str());
    lv_point_t lower_size,upper_size;
    lv_text_get_size(&lower_size,labels.first.c_str(),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    lv_text_get_size(&upper_size,labels.second.c_str(),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    plot_left=std::min(onx_geom::S(66),static_cast<int>(std::max(lower_size.x,upper_size.x))+5);
    for(auto *label:{minimum_label,maximum_label}) {
      lv_obj_set_width(label,plot_left-5);
      lv_obj_clear_flag(label,LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(maximum_label,LV_ALIGN_TOP_LEFT,0,0);
    lv_obj_align(minimum_label,LV_ALIGN_BOTTOM_LEFT,0,0);
    for(const auto &p:history) {
      if(count>=pixels.size())break;
      using onx_geom::S;
      pixels[count++]={int16_t(plot_right-(now-p.epoch)*(plot_right-plot_left)/range_seconds),int16_t(high>low?plot_bottom-(p.value-low)*plot_bottom/(high-low):plot_bottom/2),p.epoch};
    }
  }
  lv_obj_invalidate(canvas);
}
}
#endif
