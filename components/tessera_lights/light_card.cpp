#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "ground.h"
#include "tessera_lights.h"

// private LVGL header (misc/cache/instance/lv_image_cache.h), not in lvgl.h
extern "C" void lv_image_cache_drop(const void *src);

namespace esphome::tessera_lights {

using tessera::Font;
namespace ui = tessera::ui;

namespace {

constexpr int KELVIN_LOW = 2000, KELVIN_HIGH = 6500;
constexpr uint32_t ICON_PALETTE = 0xF03D8, ICON_THERMOMETER = 0xF050F, ICON_BULB = 0xF0335;
constexpr uint32_t HOLD_MS = 4000;

// A hue slider that sends colour names: Home Assistant's light.turn_on takes a colour as a name in a text field, not as
// a list of numbers, so the hue is steered to the nearest of these (their hue in degrees).
struct Named {
  int hue;
  const char *name;
};
constexpr Named NAMED[] = {{0, "red"},        {16, "orangered"},   {39, "orange"},     {51, "gold"},        {60, "yellow"},
                           {90, "chartreuse"}, {120, "lime"},       {150, "springgreen"}, {180, "cyan"},     {195, "deepskyblue"},
                           {210, "dodgerblue"}, {240, "blue"},      {271, "blueviolet"}, {300, "magenta"},   {328, "deeppink"},
                           {348, "crimson"},  {360, "red"}};

const char *name_of_hue(int hue) {
  const Named *best = &NAMED[0];
  for (const Named &n : NAMED)
    if (std::abs(n.hue - hue) < std::abs(best->hue - hue)) best = &n;
  return best->name;
}

int hue_of(uint32_t rgb) {
  const float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
  const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
  if (d < 1e-4f) return 0;
  float h = hi == r ? std::fmod((g - b) / d, 6.0f) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
  h *= 60;
  return (int) std::lround(h < 0 ? h + 360 : h);
}

lv_color_t hue_colour(float hue) {
  const float h = hue / 60.0f, x = 1 - std::fabs(std::fmod(h, 2.0f) - 1);
  float r = 0, g = 0, b = 0;
  if (h < 1) r = 1, g = x;
  else if (h < 2) r = x, g = 1;
  else if (h < 3) g = 1, b = x;
  else if (h < 4) g = x, b = 1;
  else if (h < 5) r = x, b = 1;
  else r = 1, b = x;
  return lv_color_make((uint8_t) (r * 255), (uint8_t) (g * 255), (uint8_t) (b * 255));
}

}  // namespace

// The menu a hold on a light opens, as Tessera's own light card has it: three glass fields, each with a symbol, a name and
// its value, and a slider in it: the colour (a rainbow), the colour temperature and the brightness.
class LightCard : public tessera::Card {
 public:
  explicit LightCard(TesseraLights *plugin) : plugin_(plugin) {}
  ~LightCard() override {
    for (Strip &s : strips_) {
      if (s.px == nullptr) continue;
      lv_image_cache_drop(&s.dsc);
      heap_caps_free(s.px);
    }
  }

  void open(const tessera::CardContext &c) override {
    entity_ = c.entity != nullptr ? c.entity : "";
    const LightInfo &info = plugin_->info(entity_);
    const int w = std::min(c.width - 2 * ui::px(24), ui::mm(130));
    const int x = (c.width - w) / 2, gap = ui::px(12);
    const int pad = ui::px(14), slider_h = ui::mm(8);
    const int panel_h = pad + ui::line_height(Font::BODY_LARGE) + ui::px(10) + slider_h + pad;
    const int panels = 1 + (info.color ? 1 : 0) + (info.ct ? 1 : 0);
    const int total = panels * panel_h + (panels - 1) * gap;
    int y = std::max(ui::px(12), (c.height - total) / 2 - ui::px(10));

    if (info.color) {
      hue_ = slider_(c.parent, x, y, w, ICON_PALETTE, plugin_->text("menu_colors"), 0, 359,
                     info.rgb != 0 ? hue_of(info.rgb) : 30, value_hue_, strip_(w - 2 * pad, slider_h, 0));
      lv_obj_add_event_cb(hue_, on_hue_, LV_EVENT_ALL, this);
      y += panel_h + gap;
    }
    if (info.ct) {
      temp_ = slider_(c.parent, x, y, w, ICON_THERMOMETER, plugin_->text("menu_temperature"), KELVIN_LOW, KELVIN_HIGH,
                      std::clamp(info.kelvin > 0 ? info.kelvin : 2700, KELVIN_LOW, KELVIN_HIGH), value_temp_,
                      strip_(w - 2 * pad, slider_h, 1));
      lv_obj_add_event_cb(temp_, on_temp_, LV_EVENT_ALL, this);
      y += panel_h + gap;
    }
    bright_ = slider_(c.parent, x, y, w, ICON_BULB, plugin_->text("menu_brightness"), 1, 100, std::max(info.percent, 1),
                      value_bright_, nullptr);
    // The brightness bar in the light's own colour, the rest of it dim.
    const uint32_t colour = info.rgb != 0 ? info.rgb : info.kelvin > 0 ? kelvin_rgb(info.kelvin) : 0xFFB46B;
    lv_obj_set_style_bg_color(bright_, lit(colour, 120), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bright_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bright_, lv_color_hex(colour), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bright_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_add_event_cb(bright_, on_bright_, LV_EVENT_ALL, this);
    // The brightness knob is a thin tick, as Tessera draws it, not a ring.
    lv_obj_set_style_bg_color(bright_, lv_color_hex(0xF4F1EC), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(bright_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(bright_, 0, LV_PART_KNOB);
    lv_obj_set_style_radius(bright_, ui::px(2), LV_PART_KNOB);
    lv_obj_set_style_pad_hor(bright_, ui::px(2), LV_PART_KNOB);
    lv_obj_set_style_pad_ver(bright_, -ui::px(8), LV_PART_KNOB);
    show_();
  }

  void on_state(JsonObjectConst data) override {
    if (millis() < hold_until_) return;
    LightInfo &info = plugin_->info(entity_);
    info.on = std::string(data["state"] | "") == "on";
    if (bright_ != nullptr && info.on && info.percent > 0) lv_slider_set_value(bright_, info.percent, LV_ANIM_OFF);
    show_();
  }

  void on_theme() override { show_(); }

 protected:
  struct Strip {
    uint8_t *px = nullptr;
    lv_image_dsc_t dsc{};
  };

  // The picture under a slider: 0 the rainbow, 1 the colour temperature from warm to cold, dithered so it shows no bands.
  const lv_image_dsc_t *strip_(int w, int h, int kind) {
    strips_.resize(2);
    Strip &s = strips_[kind];
    s.px = static_cast<uint8_t *>(heap_caps_malloc((size_t) w * h * 2, MALLOC_CAP_SPIRAM));
    if (s.px == nullptr) return nullptr;
    dither(reinterpret_cast<uint16_t *>(s.px), w, h, [&](int x, int, float *want) {
      const float t = (float) x / std::max(1, w - 1);
      if (kind == 0) {
        const lv_color_t c = hue_colour(t * 359);
        want[0] = c.red, want[1] = c.green, want[2] = c.blue;
      } else {
        const uint32_t rgb = kelvin_rgb((int) (KELVIN_LOW + t * (KELVIN_HIGH - KELVIN_LOW)));
        want[0] = (rgb >> 16) & 0xFF, want[1] = (rgb >> 8) & 0xFF, want[2] = rgb & 0xFF;
      }
    });
    describe(s.dsc, s.px, w, h);
    return &s.dsc;
  }

  // A glass field: symbol, name and value on one line, the slider under it. `track` is the picture the slider runs over.
  lv_obj_t *slider_(lv_obj_t *parent, int x, int y, int w, uint32_t icon, const char *text, int low, int high, int value,
                    lv_obj_t *&value_label, const lv_image_dsc_t *track) {
    const int pad = ui::px(14), slider_h = ui::mm(8);
    const int h = pad + ui::line_height(Font::BODY_LARGE) + ui::px(10) + slider_h + pad;
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_radius(box, ui::px(18), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(box, 18, 0);  // white 7 %, as Tessera's glass
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(box, 28, 0);

    lv_obj_t *glyph = ui::label(box, Font::ICON_SMALL, theme::MUTED);
    lv_obj_set_pos(glyph, pad, pad);
    ui::set_text(glyph, ui::icon(icon));
    const int icon_w = ui::line_height(Font::ICON_SMALL) + ui::px(8);
    lv_obj_t *label = ui::label(box, Font::BODY_LARGE, theme::INK);
    lv_obj_set_pos(label, pad + icon_w, pad);
    lv_obj_set_size(label, w / 2, ui::line_height(Font::BODY_LARGE));
    ui::set_text(label, text);
    value_label = ui::label(box, Font::BODY_LARGE, theme::INK);
    lv_obj_set_pos(value_label, w / 2, pad);
    lv_obj_set_size(value_label, w / 2 - pad, ui::line_height(Font::BODY_LARGE));
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);

    const int sy = pad + ui::line_height(Font::BODY_LARGE) + ui::px(10), sw = w - 2 * pad;
    if (track != nullptr) {
      lv_obj_t *image = lv_image_create(box);
      lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
      lv_image_set_src(image, track);
      lv_obj_set_pos(image, pad, sy);
      lv_obj_set_style_radius(image, slider_h / 2, 0);
      lv_obj_set_style_clip_corner(image, true, 0);
    }
    lv_obj_t *slider = lv_slider_create(box);
    lv_obj_set_pos(slider, pad, sy);
    lv_obj_set_size(slider, sw, slider_h);
    lv_slider_set_range(slider, low, high);
    lv_slider_set_value(slider, std::clamp(value, low, high), LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, slider_h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, slider_h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, track != nullptr ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    // The knob a ring, as Tessera draws it.
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, ui::px(3), LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0xF4F1EC), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, ui::px(3), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, ui::px(10));
    return slider;
  }

  void show_() {
    // The rings are filled with the colour they stand at, as Tessera's are.
    if (hue_ != nullptr) {
      ui::set_text(value_hue_, std::to_string(lv_slider_get_value(hue_)) + "°");
      lv_obj_set_style_bg_color(hue_, hue_colour((float) lv_slider_get_value(hue_)), LV_PART_KNOB);
      lv_obj_set_style_bg_opa(hue_, LV_OPA_COVER, LV_PART_KNOB);
    }
    if (temp_ != nullptr) {
      ui::set_text(value_temp_, std::to_string(lv_slider_get_value(temp_)) + " K");
      lv_obj_set_style_bg_color(temp_, lv_color_hex(kelvin_rgb(lv_slider_get_value(temp_))), LV_PART_KNOB);
      lv_obj_set_style_bg_opa(temp_, LV_OPA_COVER, LV_PART_KNOB);
    }
    ui::set_text(value_bright_, std::to_string(lv_slider_get_value(bright_)) + " %");
  }

  static void on_hue_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
      card->show_();
    } else if (code == LV_EVENT_RELEASED) {
      card->hold_until_ = millis() + HOLD_MS;
      tessera::action("light.turn_on", card->entity_, "color_name", name_of_hue(lv_slider_get_value(card->hue_)));
    }
  }

  static void on_temp_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
      card->show_();
    } else if (code == LV_EVENT_RELEASED) {
      card->hold_until_ = millis() + HOLD_MS;
      tessera::action("light.turn_on", card->entity_, "color_temp_kelvin", std::to_string(lv_slider_get_value(card->temp_)));
    }
  }

  static void on_bright_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
      card->show_();
    } else if (code == LV_EVENT_RELEASED) {
      LightInfo &info = card->plugin_->info(card->entity_);
      info.on = true;
      info.percent = lv_slider_get_value(card->bright_);
      card->hold_until_ = millis() + HOLD_MS;
      tessera::action("light.turn_on", card->entity_, "brightness_pct", std::to_string(info.percent));
    }
  }

  TesseraLights *plugin_;
  std::string entity_;
  lv_obj_t *hue_{}, *temp_{}, *bright_{}, *value_hue_{}, *value_temp_{}, *value_bright_{};
  std::vector<Strip> strips_;
  uint32_t hold_until_{0};
};

tessera::Card *make_light_card(TesseraLights *plugin) { return new LightCard(plugin); }

}  // namespace esphome::tessera_lights
