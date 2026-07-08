#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "display.h"
#include "measure.h"
#include "cal.h"
#include "dac_ad5689r.h"

// palette (matches the dual-PDH controller look)
#define COL_BG     0x0000u
#define COL_PANEL  0x10C4u
#define COL_RULE   0x1926u
#define COL_INK    0xEF7Eu
#define COL_DIM    0x84B5u
#define COL_ACC    0x2EB9u   // teal
#define COL_WARN   0xFDE4u   // amber
#define COL_ERR    0xFACDu   // red
#define COL_OK     0x3714u   // green

enum Tab : uint8_t { TAB_OPER = 0, TAB_SET, TAB_CAL, TAB_COUNT };
static Tab     s_tab = TAB_OPER;
static int8_t  s_cursor = 0;
static bool    s_full_redraw = true;

// ------------------------------------------------------------
// Touchscreen — XPT2046 on SPI1, tap-on-release-free simple
// scheme: accept a point on touch-down, then lock out repeats.
// ------------------------------------------------------------
static XPT2046_Touchscreen s_ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
static uint32_t s_touch_last_ms = 0;
static bool     s_touch_was_down = false;
static int16_t  s_raw_x = -1, s_raw_y = -1;   // last raw point
static int16_t  s_map_x = -1, s_map_y = -1;   // last mapped point

static void touch_map(int16_t rx, int16_t ry, int16_t* sx, int16_t* sy) {
#if TS_SWAP_XY
  int16_t t = rx; rx = ry; ry = t;
#endif
  long x = map(rx, TS_RAW_MIN, TS_RAW_MAX, 0, 319);
  long y = map(ry, TS_RAW_MIN, TS_RAW_MAX, 0, 239);
#if TS_FLIP_X
  x = 319 - x;
#endif
#if TS_FLIP_Y
  y = 239 - y;
#endif
  *sx = (int16_t)constrain(x, 0L, 319L);
  *sy = (int16_t)constrain(y, 0L, 239L);
}

// Returns true once per tap with mapped screen coordinates.
static bool touch_tap(uint32_t now, int16_t* sx, int16_t* sy) {
  bool down = s_ts.touched();
  bool tap = false;
  if (down && !s_touch_was_down && now - s_touch_last_ms > TS_DEBOUNCE_MS) {
    TS_Point p = s_ts.getPoint();
    s_raw_x = p.x; s_raw_y = p.y;
    touch_map(p.x, p.y, &s_map_x, &s_map_y);
    *sx = s_map_x; *sy = s_map_y;
    s_touch_last_ms = now;
    tap = true;
  }
  s_touch_was_down = down;
  return tap;
}

void display_touch_debug(Print& o) {
  if (s_raw_x < 0) { o.println(F("no touch seen yet — tap the screen")); return; }
  o.print(F("raw x=")); o.print(s_raw_x);
  o.print(F(" y="));    o.print(s_raw_y);
  o.print(F("  ->  screen x=")); o.print(s_map_x);
  o.print(F(" y="));             o.println(s_map_y);
  o.println(F("tune TS_RAW_MIN/MAX + TS_SWAP_XY/TS_FLIP_* in config.h"));
}

// ------------------------------------------------------------
// Settings tab rows
// ------------------------------------------------------------
enum SetRow : int8_t {
  SR_RANGE = 0, SR_AUTOR, SR_MODE, SR_CHOP, SR_SETTLE,
  SR_APER, SR_AZINT, SR_BRIGHT, SR_DEMO, SR_COUNT
};
static const char* SET_LABEL[SR_COUNT] = {
  "Range", "Autorange", "Mode", "Chop half [ms]", "Settle [ms]",
  "Aperture [ms]", "AZ interval [s]", "Backlight", "Demo mode"
};

static void set_row_step(NvState& s, int8_t row, int dir, bool* dirty) {
  *dirty = true;
  switch (row) {
    case SR_RANGE:
      measure_set_range(s, s.range == RANGE_20MV ? RANGE_200MV : RANGE_20MV);
      break;
    case SR_AUTOR:  s.autorange = !s.autorange; break;
    case SR_MODE:
      s.mode = (s.mode == MODE_LOWZ_CHOP) ? MODE_HIGHZ_AZ : MODE_LOWZ_CHOP;
      if (s.running) { measure_stop(s); measure_start(s); }
      break;
    case SR_CHOP:
      s.chop_half_ms = constrain(s.chop_half_ms + dir * 5, 5, 10000);
      break;
    case SR_SETTLE:
      s.settle_ms = constrain(s.settle_ms + dir * 20, 20, 5000);
      break;
    case SR_APER:
      s.aperture_ms = constrain(s.aperture_ms + dir * 250, 250, 60000);
      break;
    case SR_AZINT:
      s.az_interval_s = constrain((int)s.az_interval_s + dir * 5, 0, 3600);
      break;
    case SR_BRIGHT: {
      int b = s.backlight + dir;
      if (b > BL_LEVEL_MAX) b = BL_LEVEL_MIN;       // wrap for tap-to-step
      if (b < BL_LEVEL_MIN) b = BL_LEVEL_MAX;
      s.backlight = (uint8_t)b;
      display_backlight(s.backlight);
      break;
    }
    case SR_DEMO:
      s.demo_mode = !s.demo_mode;   // takes effect on reboot (noted on screen)
      break;
  }
}

static void set_row_value(const NvState& s, int8_t row, char* buf, size_t n) {
  switch (row) {
    case SR_RANGE:  snprintf(buf, n, "%s", nv_range_name(s.range)); break;
    case SR_AUTOR:  snprintf(buf, n, "%s", s.autorange ? "ON" : "OFF"); break;
    case SR_MODE:   snprintf(buf, n, "%s", nv_mode_name(s.mode)); break;
    case SR_CHOP:   snprintf(buf, n, "%u", s.chop_half_ms); break;
    case SR_SETTLE: snprintf(buf, n, "%u", s.settle_ms); break;
    case SR_APER:   snprintf(buf, n, "%u", s.aperture_ms); break;
    case SR_AZINT:  snprintf(buf, n, "%u", s.az_interval_s); break;
    case SR_BRIGHT: snprintf(buf, n, "%u / 5", s.backlight); break;
    case SR_DEMO:   snprintf(buf, n, "%s (reboot)", s.demo_mode ? "ON" : "OFF"); break;
  }
}

// ------------------------------------------------------------
// Calibration tab rows (actions first, then read-only info)
// ------------------------------------------------------------
enum CalRow : int8_t { CR_ZERO = 0, CR_BIAS, CR_ABORT, CR_ACTIONS };

// ------------------------------------------------------------
// Shared SEL action for the current tab/cursor — used by both
// the SEL button and a re-tap on the selected row.
// ------------------------------------------------------------
static void activate(NvState& s, bool* dirty) {
  switch (s_tab) {
    case TAB_OPER:
      if (s.running) measure_stop(s); else measure_start(s);
      break;
    case TAB_SET:
      set_row_step(s, s_cursor, +1, dirty);
      break;
    case TAB_CAL:
      if (s_cursor == CR_ZERO)  cal_start_zero(s);
      if (s_cursor == CR_BIAS)  cal_start_bias(s);
      if (s_cursor == CR_ABORT) cal_abort(s);
      break;
    default: break;
  }
}

static void goto_tab(Tab t) {
  if (t == s_tab) return;
  s_tab = t;
  s_cursor = 0;
  s_full_redraw = true;
}

// Tap hit-testing. Geometry mirrors the drawing code below.
static void touch_dispatch(NvState& s, int16_t x, int16_t y, bool* dirty) {
  // Tab bar: 4..99 / 104..199 / 204..319, y < 26
  if (y < 26) {
    if      (x >= 4   && x < 100) goto_tab(TAB_OPER);
    else if (x >= 104 && x < 200) goto_tab(TAB_SET);
    else if (x >= 204)            goto_tab(TAB_CAL);
    return;
  }
  switch (s_tab) {
    case TAB_OPER:
      // big reading area = run/stop toggle
      if (y >= 30 && y < 114) activate(s, dirty);
      break;
    case TAB_SET:
      if (y >= 32 && y < 32 + SR_COUNT * 21) {
        int8_t row = (int8_t)((y - 32) / 21);
        if (row == s_cursor) activate(s, dirty);   // re-tap = step value
        else s_cursor = row;
      }
      break;
    case TAB_CAL:
      if (y >= 32 && y < 32 + CR_ACTIONS * 21) {
        int8_t row = (int8_t)((y - 32) / 21);
        if (row == s_cursor) activate(s, dirty);   // re-tap = run action
        else s_cursor = row;
      }
      break;
    default: break;
  }
}

// ------------------------------------------------------------
// Touch handling (called every loop pass). The UI is driven by
// the XPT2046 touchscreen and the web dashboard only — there are
// no physical front-panel buttons on the digital board.
// ------------------------------------------------------------
void display_input(NvState& s, uint32_t now, bool* dirty) {
  int16_t tx, ty;
  if (touch_tap(now, &tx, &ty)) touch_dispatch(s, tx, ty, dirty);
}

// ------------------------------------------------------------
// Rendering helpers
// ------------------------------------------------------------
static void tab_bar(Adafruit_ILI9341& t) {
  static const char* NAME[TAB_COUNT] = { "OPERATION", "SETTINGS", "CALIBRATION" };
  t.fillRect(0, 0, 320, 24, COL_BG);
  int x = 4;
  for (int i = 0; i < TAB_COUNT; i++) {
    int w = (i == 2) ? 116 : 96;
    bool on = (i == (int)s_tab);
    t.fillRoundRect(x, 2, w, 20, 4, on ? COL_ACC : COL_PANEL);
    t.setTextColor(on ? COL_BG : COL_DIM);
    t.setTextSize(1);
    t.setCursor(x + 8, 8);
    t.print(NAME[i]);
    x += w + 4;
  }
  t.drawFastHLine(0, 24, 320, COL_RULE);
}

static void fmt_volts(double v, char* buf, size_t n) {
  if (isnan(v))            snprintf(buf, n, "  ---.---");
  else if (fabs(v) < 1e-3) snprintf(buf, n, "%+9.3f uV", v * 1e6);
  else                     snprintf(buf, n, "%+9.4f mV", v * 1e3);
}

static void draw_oper(Adafruit_ILI9341& t, NvState& s, const char* ip) {
  char buf[40];

  // Big reading
  t.fillRect(0, 30, 320, 60, COL_BG);
  fmt_volts(s.reading_v, buf, sizeof(buf));
  t.setTextSize(3);
  t.setTextColor(s.overload ? COL_ERR : COL_INK);
  t.setCursor(10, 44);
  t.print(buf);
  if (s.overload) {
    t.setTextSize(2); t.setTextColor(COL_ERR);
    t.setCursor(240, 50); t.print("OVLD");
  }

  // Status badges
  t.fillRect(0, 92, 320, 22, COL_BG);
  t.setTextSize(1);
  t.setCursor(10, 98);
  t.setTextColor(s.running ? COL_OK : COL_DIM);
  t.print(s.running ? "RUN " : "STOP");
  t.setTextColor(COL_DIM);
  snprintf(buf, sizeof(buf), "  %s%s  %s  phase:%s",
           nv_range_name(s.range), s.autorange ? "(auto)" : "",
           nv_mode_name(s.mode), nv_phase_name(s.phase));
  t.print(buf);

  // Statistics panel
  t.fillRect(6, 120, 308, 84, COL_PANEL);
  t.drawRect(6, 120, 308, 84, COL_RULE);
  t.setTextColor(COL_DIM);  t.setCursor(14, 128); t.print("mean");
  t.setTextColor(COL_DIM);  t.setCursor(14, 148); t.print("sigma");
  t.setTextColor(COL_DIM);  t.setCursor(14, 168); t.print("p-p");
  t.setTextColor(COL_DIM);  t.setCursor(14, 188); t.print("N");
  t.setTextColor(COL_INK);
  fmt_volts(s.stats.mean, buf, sizeof(buf));
  t.setCursor(70, 128); t.print(buf);
  if (s.stats.n > 1) {
    snprintf(buf, sizeof(buf), "%9.1f nV", s.stats.sigma() * 1e9);
    t.setCursor(70, 148); t.print(buf);
    snprintf(buf, sizeof(buf), "%9.1f nV", (s.stats.vmax - s.stats.vmin) * 1e9);
    t.setCursor(70, 168); t.print(buf);
  }
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)s.stats.n);
  t.setCursor(70, 188); t.print(buf);

  // AZ age (high-Z) / chop state
  t.setTextColor(COL_DIM);
  t.setCursor(180, 128);
  if (s.mode == MODE_HIGHZ_AZ) {
    snprintf(buf, sizeof(buf), "AZ age: %lus", (unsigned long)s.az_age_s);
    t.print(buf);
  } else {
    snprintf(buf, sizeof(buf), "chop: %s", s.chop_rev ? "REV" : "FWD");
    t.print(buf);
  }
  t.setCursor(180, 148);
  snprintf(buf, sizeof(buf), "ADC: %+7.4f V", s.adc_v);
  t.print(buf);
  if (s.demo_mode) {
    t.setTextColor(COL_WARN);
    t.setCursor(180, 188);
    t.print("DEMO MODE");
  }

  // Footer
  t.fillRect(0, 214, 320, 26, COL_BG);
  t.setTextColor(COL_DIM);
  t.setCursor(10, 222);
  t.print("IP "); t.print(ip);
}

static void draw_settings(Adafruit_ILI9341& t, NvState& s) {
  char buf[32];
  t.fillRect(0, 30, 320, 184, COL_BG);
  t.setTextSize(1);
  for (int i = 0; i < SR_COUNT; i++) {
    int y = 34 + i * 21;
    bool cur = (i == s_cursor);
    t.fillRect(6, y - 2, 308, 19, cur ? COL_PANEL : COL_BG);
    t.setTextColor(cur ? COL_ACC : COL_DIM);
    t.setCursor(14, y + 2);
    t.print(SET_LABEL[i]);
    set_row_value(s, i, buf, sizeof(buf));
    t.setTextColor(COL_INK);
    t.setCursor(190, y + 2);
    t.print(buf);
  }
  t.fillRect(0, 214, 320, 26, COL_BG);
}

static void draw_cal(Adafruit_ILI9341& t, NvState& s) {
  char buf[64];
  t.fillRect(0, 30, 320, 184, COL_BG);
  t.setTextSize(1);

  static const char* ACT[CR_ACTIONS] = {
    "Run ZERO cal (shorted offset)",
    "Run BIAS cal (100k standard)",
    "Abort calibration",
  };
  for (int i = 0; i < CR_ACTIONS; i++) {
    int y = 34 + i * 21;
    bool cur = (i == s_cursor);
    t.fillRect(6, y - 2, 308, 19, cur ? COL_PANEL : COL_BG);
    t.setTextColor(cur ? COL_ACC : COL_INK);
    t.setCursor(14, y + 2);
    t.print(ACT[i]);
  }

  // status line
  t.setTextColor(s.cal_busy ? COL_WARN : COL_OK);
  t.setCursor(14, 102);
  snprintf(buf, sizeof(buf), "status: %s", s.cal_msg);
  t.print(buf);

  // constants
  t.fillRect(6, 116, 308, 92, COL_PANEL);
  t.drawRect(6, 116, 308, 92, COL_RULE);
  t.setTextColor(COL_DIM);
  t.setCursor(14, 124);
  snprintf(buf, sizeof(buf), "gain corr 20mV : %.6f", s.cal.gain_corr[0]);
  t.print(buf);
  t.setCursor(14, 140);
  snprintf(buf, sizeof(buf), "gain corr 200mV: %.6f", s.cal.gain_corr[1]);
  t.print(buf);
  t.setCursor(14, 156);
  snprintf(buf, sizeof(buf), "zero[%s/%s]: %+.1f nV",
           nv_mode_name(s.mode), nv_range_name(s.range),
           s.cal.zero_v[s.mode][s.range] * 1e9);
  t.print(buf);
  t.setCursor(14, 172);
  snprintf(buf, sizeof(buf), "bias servo: %+.1f pA (Ib %.1f pA)",
           s.cal.bias_pa, s.cal.ib_pa);
  t.print(buf);
  t.setCursor(14, 188);
  snprintf(buf, sizeof(buf), "live servo: %+.1f pA", bias_get_pa());
  t.print(buf);

  t.fillRect(0, 214, 320, 26, COL_BG);
}

// ------------------------------------------------------------
// Backlight — thermometer-coded resistor branches, P-FET gates
// active LOW. Branch i conducts at level >= i+1; level 1 leaves
// only the always-on 330R trickle branch.
// ------------------------------------------------------------
void display_backlight(uint8_t level) {
  level = constrain(level, (uint8_t)BL_LEVEL_MIN, (uint8_t)BL_LEVEL_MAX);
  digitalWrite(PIN_BL_1, level >= 2 ? LOW : HIGH);
  digitalWrite(PIN_BL_2, level >= 3 ? LOW : HIGH);
  digitalWrite(PIN_BL_3, level >= 4 ? LOW : HIGH);
  digitalWrite(PIN_BL_4, level >= 5 ? LOW : HIGH);
}

// ------------------------------------------------------------
void display_init(Adafruit_ILI9341& tft) {
  // gates HIGH (branches off) before switching to output — no
  // full-brightness flash between pinMode and the first write
  digitalWrite(PIN_BL_1, HIGH); pinMode(PIN_BL_1, OUTPUT);
  digitalWrite(PIN_BL_2, HIGH); pinMode(PIN_BL_2, OUTPUT);
  digitalWrite(PIN_BL_3, HIGH); pinMode(PIN_BL_3, OUTPUT);
  digitalWrite(PIN_BL_4, HIGH); pinMode(PIN_BL_4, OUTPUT);
  tft.begin();
  tft.setRotation(1);          // 320x240 landscape
  tft.fillScreen(COL_BG);
  s_ts.begin(SPI1);            // XPT2046 shares the display bus
  s_full_redraw = true;
}

void display_tick(Adafruit_ILI9341& tft, NvState& s, uint32_t now,
                  bool* dirty, const char* ip) {
  (void)now; (void)dirty;
  if (s_full_redraw) {
    tft.fillScreen(COL_BG);
    tab_bar(tft);
    s_full_redraw = false;
  }
  switch (s_tab) {
    case TAB_OPER: draw_oper(tft, s, ip); break;
    case TAB_SET:  draw_settings(tft, s); break;
    case TAB_CAL:  draw_cal(tft, s); break;
    default: break;
  }
}
