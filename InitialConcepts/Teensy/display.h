#pragma once
#include <Adafruit_ILI9341.h>
#include "nv_state.h"

// ============================================================
// TFT UI — three tabs (Operation / Settings / Calibration),
// driven by four buttons AND the XPT2046 touchscreen.
//
// Buttons: TAB cycles tabs, UP/DOWN move or adjust, SEL activates.
// Touch:   tap a tab header to switch screens; tap a row to
//          select it, tap the selected row again to activate /
//          step it; tap the big reading to run/stop.
//
//   Operation tab : SEL / tap reading = run/stop, UP/DOWN = range
//   Settings tab  : UP/DOWN or tap = cursor, SEL / re-tap = step
//   Calibration   : UP/DOWN or tap = cursor, SEL / re-tap = run
//
// display_tick() redraws at the caller's cadence; buttons and
// touch are polled every loop pass via display_buttons(). Sets
// *settings_dirty when a persisted setting is edited (the .ino
// debounces the EEPROM write).
// ============================================================

void display_init(Adafruit_ILI9341& tft);
void display_tick(Adafruit_ILI9341& tft, NvState& s, uint32_t now_ms,
                  bool* settings_dirty, const char* ip);
void display_buttons(NvState& s, uint32_t now_ms, bool* settings_dirty);
// Bring-up aid: prints last raw + mapped touch point ('touch' CLI cmd)
void display_touch_debug(Print& out);
