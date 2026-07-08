#pragma once
#include <stdint.h>
#include <math.h>

// ============================================================
// Central instrument state. One global instance (g_nv) owned by
// the .ino; every module reads/writes through a reference so the
// whole instrument can be inspected from one place in a debugger
// (or dumped with the 'state' CLI command).
// ============================================================

enum NvRange : uint8_t { RANGE_20MV = 0, RANGE_200MV = 1 };
enum NvMode  : uint8_t { MODE_LOWZ_CHOP = 0, MODE_HIGHZ_AZ = 1 };

// Measurement engine phases (visible for debugging via 'state')
enum NvPhase : uint8_t {
  PH_IDLE = 0,
  PH_SETTLE,        // waiting after a relay change
  PH_INT_A,         // integrating, chop normal polarity
  PH_INT_B,         // integrating, chop reversed polarity
  PH_INT_DC,        // integrating, high-Z mode
  PH_AZ_SETTLE,     // input shorted, settling
  PH_AZ_INT,        // integrating the zero
  PH_CAL,           // a calibration routine owns the engine
};

// Calibration constants — separate EEPROM block, own magic.
struct NvCal {
  uint32_t magic;
  float    gain_corr[2];   // multiplies nominal gain; [RANGE_20MV], [RANGE_200MV]
  float    zero_v[2][2];   // stored zero offset (input volts) [mode][range]
  float    bias_pa;        // signed servo setting for nulled input bias
  float    ib_pa;          // inferred input bias current (= -bias_pa)
  uint32_t cal_time_s;     // uptime-stamp of last cal (informational)
};

struct NvStats {
  uint32_t n = 0;
  double   mean = 0.0, m2 = 0.0;       // Welford accumulators
  double   vmin = NAN, vmax = NAN;
  void reset() { n = 0; mean = m2 = 0.0; vmin = vmax = NAN; }
  void add(double v) {
    n++;
    double d = v - mean;
    mean += d / n;
    m2   += d * (v - mean);
    if (isnan(vmin) || v < vmin) vmin = v;
    if (isnan(vmax) || v > vmax) vmax = v;
  }
  double sigma() const { return (n > 1) ? sqrt(m2 / (n - 1)) : NAN; }
};

struct NvState {
  // --- user settings (persisted via nv_settings) -------------
  NvRange  range        = RANGE_20MV;
  bool     autorange    = false;
  NvMode   mode         = MODE_LOWZ_CHOP;
  uint16_t chop_half_ms = 500;
  uint16_t settle_ms    = 100;
  uint16_t aperture_ms  = 1000;
  uint16_t az_interval_s= 30;
  uint8_t  backlight    = 3;      // TFT brightness 1..5 (DC resistor bank)

  // --- run/engine state ---------------------------------------
  bool     running   = false;
  NvPhase  phase     = PH_IDLE;
  bool     chop_rev  = false;     // K1 currently in reversed state
  bool     shorted   = false;     // K3 currently shorting the input
  bool     cal_res_in= false;     // K4 currently bridging 100k

  // --- live results (input-referred volts) --------------------
  double   reading_v   = NAN;     // last completed reading
  uint32_t reading_ms  = 0;       // millis() of last reading
  double   adc_v       = NAN;     // last raw filtered ADC sample (ADC volts)
  bool     overload    = false;
  uint32_t az_age_s    = 0;       // seconds since last auto-zero (high-Z)
  double   live_zero_v = 0.0;     // zero in use (from AZ or cal store)
  NvStats  stats;

  // --- calibration --------------------------------------------
  NvCal    cal;
  bool     cal_busy  = false;
  char     cal_msg[48] = "idle";

  // --- misc ----------------------------------------------------
  bool     demo_mode = false;
};

// Helpers shared by UI / comms
inline float nv_gain(const NvState& s) {
  float g = (s.range == RANGE_20MV) ? 201.0f : 21.0f;
  return g * s.cal.gain_corr[s.range];
}
inline float nv_range_fs(const NvState& s) {
  return (s.range == RANGE_20MV) ? 0.020f : 0.200f;
}
inline const char* nv_range_name(NvRange r) {
  return r == RANGE_20MV ? "20mV" : "200mV";
}
inline const char* nv_mode_name(NvMode m) {
  return m == MODE_LOWZ_CHOP ? "lowZ-chop" : "highZ-az";
}
inline const char* nv_phase_name(NvPhase p) {
  switch (p) {
    case PH_IDLE: return "IDLE";       case PH_SETTLE: return "SETTLE";
    case PH_INT_A: return "INT_A";     case PH_INT_B: return "INT_B";
    case PH_INT_DC: return "INT_DC";   case PH_AZ_SETTLE: return "AZ_SETTLE";
    case PH_AZ_INT: return "AZ_INT";   case PH_CAL: return "CAL";
  }
  return "?";
}
