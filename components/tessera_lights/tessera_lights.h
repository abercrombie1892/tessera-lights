#pragma once
#include <map>
#include <string>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::tessera_lights {

// What the plugin knows of one light: set by its tile from Home Assistant's state, read by the menu a hold opens (a
// card is told only the state and the name, not the attributes).
struct LightInfo {
  std::string name;
  bool on = false, unavailable = false, ct = false, color = false;
  int percent = 0;        // 0 to 100, what the light is dimmed up to
  int kelvin = 0;         // its colour temperature, 0 when it has none
  uint32_t rgb = 0;       // its colour, 0 when it has none
  std::string icon;       // the icon Home Assistant has for it ("hue:go"), "" when it has none
};

class TesseraLights : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  LightInfo &info(const std::string &entity) { return infos_[entity]; }

 protected:
  std::map<std::string, LightInfo> infos_;
};

tessera::Tile *make_light_tile(TesseraLights *plugin);
tessera::Card *make_light_card(TesseraLights *plugin);

}  // namespace esphome::tessera_lights
