#include "tessera_lights.h"

namespace esphome::tessera_lights {

void TesseraLights::setup() {
  add_tile("light", [this]() { return make_light_tile(this); });
  add_card("light", [this]() { return make_light_card(this); });
}

}  // namespace esphome::tessera_lights
