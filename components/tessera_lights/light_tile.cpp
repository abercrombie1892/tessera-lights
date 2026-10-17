#include <cstring>
#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "ground.h"
#include "hue_icons.h"
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

// The picture of an icon of the pack, by the name Home Assistant gives it ("hue:go", "phu:play"); null when it is not there.
const HueIcon *find_icon(const std::string &name) {
  size_t low = 0, high = sizeof(HUE_ICONS) / sizeof(HUE_ICONS[0]);
  while (low < high) {
    const size_t mid = (low + high) / 2;
    const int order = strcmp(HUE_ICONS[mid].name, name.c_str());
    if (order == 0) return &HUE_ICONS[mid];
    if (order < 0) low = mid + 1;
    else high = mid;
  }
  return nullptr;
}

// One of the three pictures of the card's face.
struct Face {
  uint8_t *px = nullptr;
  size_t cap = 0;
  lv_image_dsc_t dsc{};
};

}  // namespace

// A light that is its own slider. The card's own background is the picture: a faint ground in the light's colour, and over
// it the same colour, lit, as far as the light is dimmed up (from the left on a wide tile, from the foot on a tall one).
// It is one picture on the card itself, so its corners and edge are the card's, to the pixel. A short tap switches the
// light, a swipe along the tile dims it, a hold opens the plugin's menu.
class LightTile : public tessera::Tile {
 public:
  explicit LightTile(TesseraLights *plugin) : plugin_(plugin) {}
  ~LightTile() override {
    if (card_ != nullptr && lv_obj_is_valid(card_) && shown_face_) lv_obj_set_style_bg_image_src(card_, nullptr, 0);
    heap_caps_free(icon_px_);
    for (Face *f : {&base_, &lit_, &comp_}) {
      lv_image_cache_drop(&f->dsc);
      heap_caps_free(f->px);
    }
  }

  void create(const tessera::TileContext &c) override {
    card_ = lv_obj_get_parent(c.parent);
    entity_ = c.entity != nullptr ? c.entity : "";
    name_ = c.name != nullptr ? c.name : "";
    tile_ = c.tile;
    w_ = c.width;
    h_ = c.height;
    vertical_ = h_ > w_ * 12 / 10;
    percent_ = std::string(c.options["percent"] | "show") == "show";
    symbol_ = std::string(c.options["symbol"] | "show") == "show";

    state_ = ui::label(c.parent, Font::BODY, theme::INK);
    lv_obj_set_pos(state_, ui::px(8), ui::px(6));
    lv_obj_set_size(state_, w_ - ui::px(16), ui::line_height(Font::BODY));
    title_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK);
    show_name_(name_);

    // The light's icon, from Home Assistant, at the top right in white.
    icon_size_ = std::min(ui::mm(7), w_ / 4);
    icon_ = lv_image_create(c.parent);
    lv_obj_remove_flag(icon_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(icon_, icon_size_, icon_size_);
    lv_obj_set_pos(icon_, w_ - icon_size_ - ui::px(8), ui::px(8));
    lv_image_set_inner_align(icon_, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(icon_, true);
    lv_obj_set_style_opa(icon_, 235, 0);
    lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);

    // The touch layer over everything: it takes the tap, the swipe and the hold.
    hit_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(hit_);
    lv_obj_remove_flag(hit_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(hit_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(hit_, 0, 0);
    lv_obj_set_size(hit_, w_, h_);
    lv_obj_add_flag(hit_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit_, on_touch_, LV_EVENT_ALL, this);
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
    // A light that is off has no colour in Home Assistant: the tile keeps the last one it knew.
    JsonArrayConst rgb = attributes["rgb_color"];
    if (!rgb.isNull() && rgb.size() == 3)
      info.rgb = ((uint32_t) (int) rgb[0] << 16) | ((uint32_t) (int) rgb[1] << 8) | (uint32_t) (int) rgb[2];
    if (attributes["color_temp_kelvin"].is<int>()) info.kelvin = (int) attributes["color_temp_kelvin"];
    info.ct = info.color = false;
    for (JsonVariantConst mode : attributes["supported_color_modes"].as<JsonArrayConst>()) {
      const std::string m = mode | "";
      if (m == "color_temp") info.ct = true;
      if (m == "xy" || m == "hs" || m == "rgb" || m == "rgbw" || m == "rgbww") info.color = true;
    }
    const std::string icon = attributes["icon"] | "";
    if (!icon.empty()) info.icon = icon;
    if (name_.empty()) show_name_(info.name);
    show_icon_();
    refresh_();
  }

  // Tessera colours its cards again when it draws them: every tick puts the picture back (only when it is not there).
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

  // The icon Home Assistant has for the light, when the pack has it (else none).
  void show_icon_() {
    const HueIcon *icon = symbol_ ? find_icon(plugin_->info(entity_).icon) : nullptr;
    if (icon == nullptr) {
      lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    icon_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    constexpr int PIXELS = HUE_ICON_SIDE * HUE_ICON_SIDE, BYTES = PIXELS * 4;
    icon_dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
    icon_dsc_.header.w = icon_dsc_.header.h = HUE_ICON_SIDE;
    icon_dsc_.header.stride = HUE_ICON_SIDE * 4;
    icon_dsc_.data_size = BYTES;
    if (icon_shown_ != icon) {
      // The picture is made white with its alpha in PSRAM: the display's blitter cannot read the flash the pack lives in
      // (it drew noise), and it does not take an alpha-only picture either.
      if (icon_px_ == nullptr) icon_px_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, BYTES, MALLOC_CAP_SPIRAM));
      if (icon_px_ == nullptr) {
        lv_obj_add_flag(icon_, LV_OBJ_FLAG_HIDDEN);
        return;
      }
      const uint8_t *alpha = HUE_ICON_DATA + icon->offset;
      for (int i = 0; i < PIXELS; i++) {
        icon_px_[i * 4 + 0] = icon_px_[i * 4 + 1] = icon_px_[i * 4 + 2] = 255;
        icon_px_[i * 4 + 3] = alpha[i];
      }
      icon_dsc_.data = icon_px_;
      icon_shown_ = icon;
      lv_image_cache_drop(&icon_dsc_);
      lv_image_set_src(icon_, nullptr);
      lv_image_set_src(icon_, &icon_dsc_);
    }
    lv_obj_remove_flag(icon_, LV_OBJ_FLAG_HIDDEN);
  }

  void refresh_() {
    const LightInfo &info = plugin_->info(entity_);
    if (dragging_) return;
    shown_ = info.on ? std::max(info.percent, 4) : 0;
    paint_();
    ui::set_text(state_, !percent_ ? "" : info.unavailable ? plugin_->text("unavailable")
                         : info.on ? std::to_string(info.percent) + " %" : plugin_->text("off"));
    white(state_);
    white(title_);
  }

  // The card's face as one picture: the lit picture as far as `shown_` says, the faint ground beyond it.
  void compose_(int cw, int ch) {
    const int stride = cw * 2;
    if (vertical_) {
      const int lit_rows = ch * shown_ / 100;
      memcpy(comp_.px, base_.px, (size_t) (ch - lit_rows) * stride);
      memcpy(comp_.px + (size_t) (ch - lit_rows) * stride, lit_.px + (size_t) (ch - lit_rows) * stride, (size_t) lit_rows * stride);
    } else {
      const int lit_cols = cw * shown_ / 100;
      for (int y = 0; y < ch; y++) {
        uint8_t *row = comp_.px + (size_t) y * stride;
        memcpy(row, lit_.px + (size_t) y * stride, (size_t) lit_cols * 2);
        memcpy(row + lit_cols * 2, base_.px + (size_t) y * stride + lit_cols * 2, (size_t) (cw - lit_cols) * 2);
      }
    }
    lv_image_cache_drop(&comp_.dsc);
    describe(comp_.dsc, comp_.px, cw, ch);
  }

  void paint_() {
    if (card_ == nullptr || !lv_obj_is_valid(card_)) return;
    const int cw = lv_obj_get_width(card_), ch = lv_obj_get_height(card_);
    if (cw <= 0 || ch <= 0) return;
    const uint32_t colour = colour_();
    const uint32_t sign = ((uint32_t) cw * 31 + ch) * 16777619u ^ colour;
    bool changed = false;
    if (sign != signature_) {
      if (!make_(base_, cw, ch, lit(colour, 62), lit(colour, 34)) || !make_(lit_, cw, ch, lit(colour, 150), lit(colour, 96)) ||
          !alloc_(comp_, cw, ch))
        return;
      signature_ = sign;
      changed = true;
    }
    if (changed || shown_ != composed_ || (dragging_ && shown_ != composed_)) {
      compose_(cw, ch);
      composed_ = shown_;
      changed = true;
    }
    if (changed) {
      lv_obj_set_style_bg_image_src(card_, nullptr, 0);
      lv_obj_invalidate(card_);
    }
    if (lv_obj_get_style_bg_image_src(card_, LV_PART_MAIN) != &comp_.dsc) lv_obj_set_style_bg_image_src(card_, &comp_.dsc, 0);
    shown_face_ = true;
    if (lv_obj_get_style_bg_grad_dir(card_, LV_PART_MAIN) != LV_GRAD_DIR_NONE)
      lv_obj_set_style_bg_grad_dir(card_, LV_GRAD_DIR_NONE, 0);
  }

  bool alloc_(Face &f, int w, int h) {
    const size_t bytes = (size_t) w * h * 2;
    if (bytes > f.cap) {
      heap_caps_free(f.px);
      f.px = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
      f.cap = f.px == nullptr ? 0 : bytes;
      if (f.px == nullptr) return false;
    }
    return true;
  }

  bool make_(Face &f, int w, int h, lv_color_t top, lv_color_t foot) {
    if (!alloc_(f, w, h)) return false;
    gradient(reinterpret_cast<uint16_t *>(f.px), w, h, top, foot);
    lv_image_cache_drop(&f.dsc);
    describe(f.dsc, f.px, w, h);
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
        shown_ = percent_at_(at);
        paint_();
        ui::set_text(state_, percent_ ? std::to_string(shown_) + " %" : "");
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
  lv_obj_t *card_{}, *state_{}, *title_{}, *hit_{}, *icon_{};
  lv_image_dsc_t icon_dsc_{};
  const HueIcon *icon_shown_{nullptr};
  uint8_t *icon_px_{nullptr};
  int icon_size_{0};
  std::string entity_, name_;
  int tile_{-1}, w_{0}, h_{0}, shown_{0}, composed_{-1};
  bool vertical_{false}, dragging_{false}, held_{false}, percent_{true}, symbol_{true}, shown_face_{false};
  lv_point_t start_{0, 0};
  uint32_t hold_until_{0}, signature_{0};
  Face base_, lit_, comp_;
};

tessera::Tile *make_light_tile(TesseraLights *plugin) { return new LightTile(plugin); }

}  // namespace esphome::tessera_lights
