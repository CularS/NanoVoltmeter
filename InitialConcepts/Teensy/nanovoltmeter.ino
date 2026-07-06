/* Nanovoltmeter Controller — Teensy 4.1
   Front end: Nanovoltmeter/nanovoltmeter_fe.kicad_sch (this repo)

   Architecture (mirrors the dual-PDH controller):
     config.h       pin map + constants (all tuning in one place)
     nv_state.h     ONE central NvState struct — dump it with 'state'
     relays.cpp     latching-coil pulse driver (K1 chop, K2 range,
                    K3 short, K4 100k cal standard)
     adc_ltc2500.cpp LTC2500-32 filtered-output driver + demo sim
     dac_ad5689r.cpp bias-current servo DAC
     measure.cpp    measurement engine state machine (chop demod,
                    high-Z auto-zero, autorange, overload blanking)
     cal.cpp        EEPROM cal store + zero/bias/gain routines
     display.cpp    ILI9341 UI: Operation / Settings / Calibration
                    tabs, 4 buttons (TAB, UP, DOWN, SEL)
     comms.cpp      ONE command processor for USB serial, telnet
                    and the web console; JSON status
     network.cpp    QNEthernet: HTTP dashboard (80) + telnet (23)
     webpage.h      embedded dashboard (same three tabs)
     demo.cpp       simulated front end (runtime 'demo on/off')

   Debug seams:
     - 'demo on' runs everything against a simulated front end
     - 'state' dumps the whole engine; 'relay'/'bias'/'adc' poke hardware
     - every module takes Print& so output goes to whoever asked
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#include "config.h"
#include "nv_state.h"
#include "nv_settings.h"
#include "relays.h"
#include "adc_ltc2500.h"
#include "dac_ad5689r.h"
#include "measure.h"
#include "cal.h"
#include "display.h"
#include "comms.h"
#include "network.h"
#include "demo.h"

// SPI1 hardware bus: MOSI=26, SCK=27 — explicit constructor required
static Adafruit_ILI9341 tft(&SPI1, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);

NvState    g_nv;                 // the entire instrument, one struct
CommsFlags g_flags;

static String   usb_line;
static uint32_t s_dirty_ms = 0;  // settings-save debounce
static elapsedMicros loopTimer;

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  SPI.begin();    // SPI0 — ADC + DAC
  SPI1.begin();   // SPI1 — TFT

  // Load settings + cal before any hardware decisions
  NvSettings ns;
  nv_settings_load(ns);
  nv_settings_to_state(ns, g_nv);
  cal_load(g_nv.cal);

  relays_init();
  ad5689r_init();
  bias_set_pa(g_nv.cal.bias_pa);           // restore bias compensation
  adc_init(g_nv.demo_mode);
  if (g_nv.demo_mode) demo_init();

  display_init(tft);
  measure_init(g_nv);

  network_init();
  loopTimer = 0;

  Serial.println(g_nv.demo_mode
    ? F("*** DEMO MODE — 'demo off' + reboot for real hardware ***")
    : F("Real hardware mode."));
  Serial.println(F("Ready. Type 'help' for commands."));
}

// ------------------------------------------------------------
void loop() {
  uint32_t now = millis();

  // ---- USB serial CLI (line-buffered) ------------------------
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      usb_line.trim();
      if (usb_line.length())
        comms_process(usb_line, Serial, g_nv, g_flags);
      usb_line = "";
    } else {
      usb_line += c;
    }
  }

  // ---- fast housekeeping every pass ---------------------------
  relays_tick(now);
  display_buttons(g_nv, now, &g_flags.settings_dirty);

  // ---- 1 kHz engine tick --------------------------------------
  if (loopTimer >= CONTROL_PERIOD_US) {
    loopTimer = 0;
    if (g_nv.demo_mode) demo_tick(g_nv, now);
    measure_tick(g_nv, now);
  }

  // ---- 10 Hz display + network --------------------------------
  static uint32_t last_disp = 0;
  if (now - last_disp >= DISPLAY_PERIOD_MS) {
    last_disp = now;
    display_tick(tft, g_nv, now, &g_flags.settings_dirty, network_ip());
    network_poll(g_nv, g_flags);
  }

  // ---- deferred actions ---------------------------------------
  if (g_flags.settings_dirty) {
    if (s_dirty_ms == 0) s_dirty_ms = now;
    if (now - s_dirty_ms > 2000) {         // debounce EEPROM writes
      NvSettings ns;
      nv_settings_load(ns);
      nv_state_to_settings(g_nv, ns);
      nv_settings_save(ns);
      g_flags.settings_dirty = false;
      s_dirty_ms = 0;
    }
  } else {
    s_dirty_ms = 0;
  }

  if (g_flags.net_reinit) {
    g_flags.net_reinit = false;
    network_reinit();
  }

  if (g_flags.reboot) {
    delay(100);
    SCB_AIRCR = 0x05FA0004;                // ARM system reset
  }

  // ---- 2 s debug heartbeat ------------------------------------
  static uint32_t last_dbg = 0;
  if (now - last_dbg >= DEBUG_PERIOD_MS) {
    last_dbg = now;
    if (g_nv.demo_mode) Serial.print(F("[DEMO] "));
    Serial.print(nv_phase_name(g_nv.phase));
    if (!isnan(g_nv.reading_v)) {
      Serial.print(F("  V="));
      Serial.print(g_nv.reading_v * 1e6, 4);
      Serial.print(F(" uV  N="));
      Serial.print(g_nv.stats.n);
      if (g_nv.stats.n > 1) {
        Serial.print(F("  sigma="));
        Serial.print(g_nv.stats.sigma() * 1e9, 1);
        Serial.print(F(" nV"));
      }
    }
    Serial.print(F("  IP: "));
    Serial.println(network_ip());
  }
}
