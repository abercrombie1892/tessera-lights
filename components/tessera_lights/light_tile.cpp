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

constexpr uint32_t HOLD_MS = 4000;      // after a change of ours the state of Home Assistant may not yet show it
constexpr uint32_t WARM_WHITE = 0xFFB46B;

void white(lv_obj_t *label) {
  if (!lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), lv_color_hex(0xFFFFFF)))
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
}

}  // namespace

// A light that is its own slider. The tile has a faint ground in the light's colour; over it the same colour, lit, as far
// as the light is dimmed up (from the left on a wide tile, from the foot on a tall one). A short tap switches the light,
// a swipe along the tile dims it, a hold opens the plugin's menu.
class LightTile : public tessera::Tile {
 public:
  explicit LightTile(TesseraLights *plugin) : plugin_(plugin) {}
  ~LightTile() override {
    if (card_ != nullptr && lv_obj_is_valid(card_) && base_px_ != nullptr) lv_obj_set_style_bg_image_src(card_, nullptr, 0);
    for (auto *px : {&base_px_, &lit_px_}) {
      if (*px == nullptr) continue;
      heap_caps_free(*px);
      *px = nullptr;
    }
    lv_image_cache_drop(&base_);
    lv_image_cache_drop(&lit_);
  }

  void create(const tessera::TileContext &c) override {
    parent_ = c.parent;
    card_ = lv_obj_get_parent(c.parent);
    entity_ = c.entity != nullptr ? c.entity : "";
    name_ = c.name != nullptr ? c.name : "";
    tile_ = c.tile;
    w_ = c.width;
    h_ = c.height;
    vertical_ = h_ > w_ * 12 / 10;
    percent_ = std::string(c.options["percent"] | "show") == "show";

    lv_obj_add_flag(c.parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);  // the lit part reaches the card's edge, over its padding
    fill_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(fill_);
    lv_obj_remove_flag(fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(fill_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(fill_, ui::px(14), 0);
    lv_obj_set_style_clip_corner(fill_, true, 0);
    fill_image_ = lv_image_create(fill_);
    lv_obj_remove_flag(fill_image_, LV_OBJ_FLAG_CLICKABLE);

    state_ = ui::label(c.parent, Font::BODY, theme::INK);
    lv_obj_set_pos(state_, ui::px(8), ui::px(6));
    lv_obj_set_size(state_, w_ - ui::px(16), ui::line_height(Font::BODY));
    title_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    show_name_(name_);

    // The touch layer over everything: it takes the tap, the swipe and the hold.
    hit_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(hit_);
    lv_obj_remove_flag(hit_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hit_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(hit_, 0, 0);
    lv_obj_set_size(hit_, w_, h_);
    lv_obj_add_flag(hit_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit_, on_touch_, LV_EVENT_ALL, this);
    set_fill_(0);
    paint_();
  }

  void on_state(JsonObjectConst data) override {
    LightInfo &info = plugin_->info(entity_);
    const std::string state = data["state"] | "";
    JsonObjectConst attributes = data["attributes"];
    const std::string name = data["name"] | "";
    if (!name.empty()) info.name = name;
    info.unavailable = state == "unavailable";
    if (millis() >= hold_until_) {
      info.on = state == "on";
      info.percent = info.on && attributes["brightness"].is<int>() ? ((int) attributes["brightness"] * 100 + 127) / 255 : info.on ? 100 : 0;
    }
    info.rgb = 0;
    JsonArrayConst rgb = attributes["rgb_color"];
    if (!rgb.isNull() && rgb.size() == 3)
      info.rgb = ((uint32_t) (int) rgb[0] << 16) | ((uint32_t) (int) rgb[1] << 8) | (uint32_t) (int) rgb[2];
    info.kelvin = attributes["color_temp_kelvin"].is<int>() ? (int) attributes["color_temp_kelvin"] : 0;
    info.ct = info.color = false;
    for (JsonVariantConst mode : attributes["supported_color_modes"].as<JsonArrayConst>()) {
      const std::string m = mode | "";
      if (m == "color_temp") info.ct = true;
      if (m == "xy" || m == "hs" || m == "rgb" || m == "rgbw" || m == "rgbww") info.color = true;
    }
    if (name_.empty()) show_name_(info.name);
    refresh_();
  }

  // Tessera colours its cards again when it draws them: every tick puts the ground back (only when it is not there).
  void on_tick(uint32_t) override { paint_(); }
  void on_theme() override { paint_(); }
  void on_tap() override {}

 protected:
  uint32_t colour_() const {
    const LightInfo &info = plugin_->info(entity_);
    if (info.rgb != 0) return info.rgb;
    if (info.kelvin > 0) return kelvin_rgb(info.kelvin);
    return WARM_WHITE;
  }

  void refresh_() {
    const LightInfo &info = plugin_->info(entity_);
    if (dragging_) return;
    set_fill_(info.on ? std::max(info.percent, 4) : 0);
    paint_();
    ui::set_text(state_, !percent_ ? "" : info.unavailable ? plugin_->text("unavailable")
                         : info.on ? std::to_string(info.percent) + " %" : plugin_->text("off"));
    white(state_);
    white(title_);
  }

  // The lit part: as wide (or as high) as the light is dimmed up, of the tile.
  void set_fill_(int percent) {
    shown_ = std::clamp(percent, 0, 100);
    lv_obj_set_style_opa(fill_, shown_ == 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    // The card's whole face, measured on the glass (the tile's area and what lies round it): the lit part runs from edge to
    // edge with the very corners of the card, whatever the padding.
    int cw = w_, ch = h_, ox = 0, oy = 0;
    if (card_ != nullptr && lv_obj_is_valid(card_)) {
      lv_area_t ca, pa;
      lv_obj_get_coords(card_, &ca);
      lv_obj_get_coords(parent_, &pa);
      if (lv_area_get_width(&ca) >= w_ && lv_area_get_height(&ca) >= h_) {
        cw = lv_area_get_width(&ca), ch = lv_area_get_height(&ca);
        ox = ca.x1 - pa.x1, oy = ca.y1 - pa.y1;
      }
      lv_obj_set_style_radius(fill_, lv_obj_get_style_radius(card_, LV_PART_MAIN), 0);
    }
    if (vertical_) {
      const int fh = ch * shown_ / 100;
      lv_obj_set_size(fill_, cw, fh);
      lv_obj_set_pos(fill_, ox, oy + ch - fh);
      lv_obj_set_pos(fill_image_, 0, -(ch - fh));
    } else {
      lv_obj_set_size(fill_, cw * shown_ / 100, ch);
      lv_obj_set_pos(fill_, ox, oy);
      lv_obj_set_pos(fill_image_, 0, 0);
    }
  }

  void paint_() {
    if (card_ == nullptr || !lv_obj_is_valid(card_)) return;
    const int cw = lv_obj_get_width(card_), ch = lv_obj_get_height(card_);
    if (cw <= 0 || ch <= 0) return;
    if (cw != face_w_ || ch != face_h_) {  // the card was laid out: the lit part follows its edge
      face_w_ = cw, face_h_ = ch;
      if (!dragging_) set_fill_(shown_);
    }
    const uint32_t colour = colour_();
    const uint32_t sign = ((uint32_t) cw * 31 + ch) * 16777619u ^ colour;
    if (sign != signature_) {
      if (!make_(base_px_, base_cap_, base_, cw, ch, lit(colour, 62), lit(colour, 34))) return;
      if (!make_(lit_px_, lit_cap_, lit_, cw, ch, lit(colour, 150), lit(colour, 96))) return;
      signature_ = sign;
      lv_obj_set_style_bg_image_src(card_, nullptr, 0);
      // LVGL keeps a decoded picture by its address: the same address again would show the old colour.
      lv_image_set_src(fill_image_, nullptr);
      lv_image_set_src(fill_image_, &lit_);
      lv_obj_invalidate(fill_);
      lv_obj_invalidate(card_);
    }
    if (lv_obj_get_style_bg_image_src(card_, LV_PART_MAIN) != &base_) lv_obj_set_style_bg_image_src(card_, &base_, 0);
    if (lv_obj_get_style_bg_grad_dir(card_, LV_PART_MAIN) != LV_GRAD_DIR_NONE)
      lv_obj_set_style_bg_grad_dir(card_, LV_GRAD_DIR_NONE, 0);
  }

  bool make_(uint8_t *&px, size_t &cap, lv_image_dsc_t &dsc, int w, int h, lv_color_t top, lv_color_t foot) {
    const size_t bytes = (size_t) w * h * 2;
    if (bytes > cap) {
      heap_caps_free(px);
      px = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
      cap = px == nullptr ? 0 : bytes;
      if (px == nullptr) return false;
    }
    gradient(reinterpret_cast<uint16_t *>(px), w, h, top, foot);
    lv_image_cache_drop(&dsc);
    describe(dsc, px, w, h);
    return true;
  }

  // The name as large as the tile's width takes it, at the foot.
  void show_name_(const std::string &name) {
    const Font steps[] = {Font::TITLE, Font::BODY_LARGE, Font::BODY};
    Font font = steps[2];
    for (int i = h_ >= ui::mm(24) ? 0 : 1; i < 3; i++) {
      font = steps[i];
      if (ui::text_width(name, font) <= w_ - ui::px(16)) break;
    }
    ui::set_font(title_, font);
    lv_obj_set_pos(title_, ui::px(8), h_ - ui::line_height(font) - ui::px(8));
    lv_obj_set_size(title_, w_ - ui::px(16), ui::line_height(font));
    ui::set_text(title_, name);
    white(title_);
  }

  // The percent under a finger, along the tile.
  int percent_at_(const lv_point_t &p) const {
    lv_area_t a;
    lv_obj_get_coords(hit_, &a);
    const float f = vertical_ ? 1.0f - (float) (p.y - a.y1) / std::max(1, h_) : (float) (p.x - a.x1) / std::max(1, w_);
    return (int) std::lround(std::clamp(f, 0.0f, 1.0f) * 100);
  }

  void send_percent_(int percent) {
    LightInfo &info = plugin_->info(entity_);
    hold_until_ = millis() + HOLD_MS;
    info.on = percent > 0;
    info.percent = percent;
    if (percent <= 0)
      tessera::action("light.turn_off", entity_);
    else
      tessera::action("light.turn_on", entity_, "brightness_pct", std::to_string(percent));
  }

  void toggle_() {
    LightInfo &info = plugin_->info(entity_);
    hold_until_ = millis() + HOLD_MS;
    info.on = !info.on;
    if (info.on && info.percent == 0) info.percent = 100;
    tessera::action("light.toggle", entity_);
  }

  static void on_touch_(lv_event_t *e) {
    auto *tile = static_cast<LightTile *>(lv_event_get_user_data(e));
    tile->touch_(e);
  }

  void touch_(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_point_t at{0, 0};
    if (auto *indev = lv_indev_active()) lv_indev_get_point(indev, &at);
    if (code == LV_EVENT_PRESSED) {
      start_ = at;
      dragging_ = held_ = false;
    } else if (code == LV_EVENT_PRESSING && !held_) {
      const int along = vertical_ ? std::abs(at.y - start_.y) : std::abs(at.x - start_.x);
      const int across = vertical_ ? std::abs(at.x - start_.x) : std::abs(at.y - start_.y);
      if (!dragging_ && along >= ui::px(12) && along > across) dragging_ = true;
      if (dragging_) {
        const int percent = percent_at_(at);
        set_fill_(percent);
        ui::set_text(state_, percent_ ? std::to_string(percent) + " %" : "");
        white(state_);
      }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
      if (dragging_) {
        dragging_ = false;
        send_percent_(shown_);
        refresh_();
      }
    } else if (code == LV_EVENT_LONG_PRESSED && !dragging_) {
      held_ = true;
      // The tile goes with the menu that opens: nothing of this tile is touched after it.
      tessera::open_card(plugin_->plugin_id(), "light", entity_, tile_);
    } else if (code == LV_EVENT_SHORT_CLICKED && !dragging_ && !held_) {
      toggle_();
      refresh_();
    }
  }

  TesseraLights *plugin_;
  lv_obj_t *parent_{}, *card_{}, *fill_{}, *fill_image_{}, *state_{}, *title_{}, *hit_{};
  std::string entity_, name_;
  int tile_{-1}, w_{0}, h_{0}, shown_{0}, face_w_{0}, face_h_{0};
  bool vertical_{false}, dragging_{false}, held_{false}, percent_{true};
  lv_point_t start_{0, 0};
  uint32_t hold_until_{0}, signature_{0};
  lv_image_dsc_t base_{}, lit_{};
  uint8_t *base_px_{nullptr}, *lit_px_{nullptr};
  size_t base_cap_{0}, lit_cap_{0};
};

tessera::Tile *make_light_tile(TesseraLights *plugin) { return new LightTile(plugin); }

}  // namespace esphome::tessera_lights
