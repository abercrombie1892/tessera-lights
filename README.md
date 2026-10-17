# Lights for Tessera

A light as a tile that is its own slider, for the ESP32-P4 screens. The tile has a faint ground in the light's colour
and glows in that colour as far as the light is dimmed up.

## Set up

1. In Tessera, Plugins, add this repository by its link and choose your screen. The screen builds once.
2. In Layout, add the tile **Light** from the library and choose the light under Entity.
3. A short tap switches the light. A swipe over the tile dims it (sideways on a wide tile, up and down on a tall one).
   Holding opens a menu with the brightness, the colour temperature and colours.

## Good to know

- Tessera's own detail card cannot be opened from a plugin tile, so the menu is the plugin's own.
- No connection of its own: Tessera and Home Assistant do the work.
