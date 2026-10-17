#include <string>
#include "ground.h"
#include "tessera_lights.h"

namespace esphome::tessera_lights {

using tessera::Font;
namespace ui = tessera::ui;

namespace {

struct Swatch {
  const char *name;  // the colour name Home Assistant's light.turn_on takes
  uint32_t rgb;
};
constexpr Swatch SWATCHES[] = {{"red", 0xFF3B30},    {"orange", 0xFF9500}, {"yellow", 0xFFD60A},
                               {"green", 0x34C759},  {"cyan", 0x32D2E6},   {"blue", 0x0A84FF},
                               {"purple", 0xAF52DE}, {"pink", 0xFF6EB4},   {"white", 0xFFF4E5}};
constexpr int KELVIN_LOW = 2000, KELVIN_HIGH = 6500;

}  // namespace

// The menu a hold on a light opens: its brightness, its colour temperature and colours, and a key that switches it.
class LightCard : public tessera::Card {
 public:
  explicit LightCard(TesseraLights *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    entity_ = c.entity != nullptr ? c.entity : "";
    LightInfo &info = plugin_->info(entity_);
    const int pad = ui::px(16), w = c.width - 2 * pad;
    int y = pad;

    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_pos(title_, pad, y);
    lv_obj_set_size(title_, w - ui::mm(18), ui::line_height(Font::TITLE));
    ui::set_text(title_, info.name);
    power_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(power_);
    lv_obj_remove_flag(power_, LV_OBJ_FLAG_SCROLLABLE);
    const int key = ui::mm(12);
    lv_obj_set_size(power_, key, key);
    lv_obj_set_pos(power_, c.width - pad - key, y - ui::px(4));
    lv_obj_set_style_radius(power_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(power_, LV_OPA_COVER, 0);
    lv_obj_add_flag(power_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(power_, on_power_, LV_EVENT_CLICKED, this);
    power_label_ = ui::label(power_, Font::BODY, theme::INK);
    lv_obj_center(power_label_);
    y += ui::line_height(Font::TITLE) + ui::px(18);

    y = slider_(c.parent, plugin_->text("menu_brightness"), pad, y, w, 1, 100, info.percent, bright_, bright_label_);
    lv_obj_add_event_cb(bright_, on_bright_, LV_EVENT_ALL, this);
    if (info.ct) {
      y = slider_(c.parent, plugin_->text("menu_temperature"), pad, y, w, KELVIN_LOW, KELVIN_HIGH,
                  info.kelvin > 0 ? info.kelvin : 2700, temp_, temp_label_);
      lv_obj_set_style_bg_color(temp_, lv_color_hex(0xFFA95C), LV_PART_MAIN);
      lv_obj_set_style_bg_grad_color(temp_, lv_color_hex(0xBFD8FF), LV_PART_MAIN);
      lv_obj_set_style_bg_grad_dir(temp_, LV_GRAD_DIR_HOR, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(temp_, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(temp_, LV_OPA_TRANSP, LV_PART_INDICATOR);
      lv_obj_add_event_cb(temp_, on_temp_, LV_EVENT_ALL, this);
    }
    if (info.color) {
      lv_obj_t *label = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_pos(label, pad, y);
      lv_obj_set_size(label, w, ui::line_height(Font::BODY));
      ui::set_text(label, plugin_->text("menu_colors"));
      y += ui::line_height(Font::BODY) + ui::px(8);
      const int count = (int) (sizeof(SWATCHES) / sizeof(SWATCHES[0]));
      const int gap = ui::px(10), side = std::min(ui::mm(12), (w - gap * (count - 1)) / count);
      for (int i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_create(c.parent);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, side, side);
        lv_obj_set_pos(dot, pad + i * (side + gap), y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(SWATCHES[i].rgb), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, ui::px(2), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(dot, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
        lv_obj_set_user_data(dot, const_cast<char *>(SWATCHES[i].name));
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(dot, on_color_, LV_EVENT_CLICKED, this);
      }
    }
    show_();
  }

  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    LightInfo &info = plugin_->info(entity_);
    if (millis() > hold_until_) {
      info.on = state == "on";
      show_();
    }
  }

  void on_theme() override { show_(); }

 protected:
  // A label and a slider under it; returns the y below them.
  int slider_(lv_obj_t *parent, const char *text, int x, int y, int w, int low, int high, int value, lv_obj_t *&slider,
              lv_obj_t *&value_label) {
    lv_obj_t *label = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w / 2, ui::line_height(Font::BODY));
    ui::set_text(label, text);
    value_label = ui::label(parent, Font::BODY, theme::INK);
    lv_obj_set_pos(value_label, x + w / 2, y);
    lv_obj_set_size(value_label, w - w / 2, ui::line_height(Font::BODY));
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    y += ui::line_height(Font::BODY) + ui::px(10);
    const int h = ui::mm(11);
    slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_range(slider, low, high);
    lv_slider_set_value(slider, std::clamp(value, low, high), LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ui::color(theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, ui::color(theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, ui::px(4), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, ui::px(10));
    return y + h + ui::px(20);
  }

  void show_() {
    const LightInfo &info = plugin_->info(entity_);
    ui::set_text(bright_label_, std::to_string(lv_slider_get_value(bright_)) + " %");
    if (temp_ != nullptr) ui::set_text(temp_label_, std::to_string(lv_slider_get_value(temp_)) + " K");
    lv_obj_set_style_bg_color(power_, info.on ? ui::color(theme::ACCENT) : ui::color(theme::TRACK), 0);
    ui::set_text(power_label_, info.on ? plugin_->text("on") : plugin_->text("off"));
    ui::set_color(power_label_, info.on ? theme::ON_ACCENT : theme::INK);
  }

  static void on_power_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    LightInfo &info = card->plugin_->info(card->entity_);
    card->hold_until_ = millis() + 4000;
    info.on = !info.on;
    tessera::action("light.toggle", card->entity_);
    card->show_();
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
      card->hold_until_ = millis() + 4000;
      tessera::action("light.turn_on", card->entity_, "brightness_pct", std::to_string(info.percent));
      card->show_();
    }
  }

  static void on_temp_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
      card->show_();
    } else if (code == LV_EVENT_RELEASED) {
      card->hold_until_ = millis() + 4000;
      tessera::action("light.turn_on", card->entity_, "color_temp_kelvin", std::to_string(lv_slider_get_value(card->temp_)));
    }
  }

  static void on_color_(lv_event_t *e) {
    auto *card = static_cast<LightCard *>(lv_event_get_user_data(e));
    auto *dot = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    const char *name = static_cast<const char *>(lv_obj_get_user_data(dot));
    card->hold_until_ = millis() + 4000;
    card->plugin_->info(card->entity_).on = true;
    tessera::action("light.turn_on", card->entity_, "color_name", name);
    card->show_();
  }

  TesseraLights *plugin_;
  std::string entity_;
  lv_obj_t *title_{}, *power_{}, *power_label_{}, *bright_{}, *bright_label_{}, *temp_{}, *temp_label_{};
  uint32_t hold_until_{0};
};

tessera::Card *make_light_card(TesseraLights *plugin) { return new LightCard(plugin); }

}  // namespace esphome::tessera_lights
