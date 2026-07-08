#pragma once
#include <Adafruit_ILI9341.h>
#include "nv_state.h"

// ============================================================
// TFT UI — three tabs (Operation / Settings / Calibration),
// driven entirely by the XPT2046 touchscreen (and the web
// dashboard). There are no physical front-panel buttons.
//
// Touch:   tap a tab header to switch screens; tap a row to
//          select it, tap the selected row again to activate /
//          step it; tap the big reading to run/stop.
//
//   Operation tab : tap reading = run/stop
//   Settings tab  : tap = cursor, re-tap = step value
//   Calibration   : tap = cursor, re-tap = run action
//
// display_tick() redraws at the caller's cadence; touch is
// polled every loop pass via display_input(). Sets
// *settings_dirty when a persisted setting is edited (the .ino
// debounces the EEPROM write).
// ============================================================

void display_init(Adafruit_ILI9341& tft);
void display_tick(Adafruit_ILI9341& tft, NvState& s, uint32_t now_ms,
                  bool* settings_dirty, const char* ip);
void display_input(NvState& s, uint32_t now_ms, bool* settings_dirty);
// TFT backlight: 5 DC levels via switched resistor branches
// (P-FET gates on PIN_BL_1..4, active LOW, thermometer-coded).
void display_backlight(uint8_t level);
// Bring-up aid: prints last raw + mapped touch point ('touch' CLI cmd)
void display_touch_debug(Print& out);
