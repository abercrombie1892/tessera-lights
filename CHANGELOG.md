# Changelog

## 0.4.2
- The icon is a white picture with alpha (ARGB) in PSRAM: the display's blitter did not draw the alpha-only picture right.

## 0.4.1
- The icon of the light shows right: its picture is copied into PSRAM, the display's blitter cannot read the flash and drew noise.

## 0.4.0
- The tile shows the icon Home Assistant has for the light at its top right, in white: the Hue icon pack (`hue:`, 369
  icons) and the two `phu:` icons the lights use are in the plugin (a 48 px picture each, 0.8 MB of flash). A light
  without an icon, or with one of another pack, shows none. The option "Symbol of the light" hides it.

## 0.3.0
- A light that is off keeps the colour it last had on the tile (Home Assistant sends none then).
- The lit part of the tile is drawn into the card's own background picture, no object of its own over the card: its corners
  and its edge are the card's, to the pixel (it was square and a little inside the card).

## 0.2.3
- The menu follows Tessera's own light card in its proportions (measured on a photo of it): fields 48 % of the width,
  11.5 % of the height, the track at the foot of each field, the block a little under the middle, the temperature
  slider from warm yellow through white to light blue.

## 0.2.2
- The menu as Tessera's: the rings of the colour and temperature sliders are filled with the colour they stand at, the
  brightness knob is a thin tick.

## 0.2.1
- A new colour of the light shows on the tile at once (LVGL showed the picture of the old colour from its cache).
- The lit part of the tile has the very corners of the card: it is measured on the glass and runs from edge to edge.

## 0.2.0
- The menu of a hold as Tessera's own light card has it: three glass fields (colour as a rainbow, colour temperature,
  brightness), each with a symbol, its name and its value. The colour goes to the light as the nearest colour name.
- The lit part of the tile runs from edge to edge of the card, in calmer colours so the white words read on it.
- The tile shows the brightness only (no "On"); the new option "Percent on the tile" hides it.

## 0.1.0
- A light as a tile that is its own slider: a ground of the light's colour, the part lit as far as the light is dimmed
  up, in the light's own colour (its colour, else the colour of its temperature). A short tap switches the light, a swipe
  over the tile dims it, holding opens a menu: brightness, colour temperature and colours.
