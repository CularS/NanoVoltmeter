#include <Arduino.h>
#include "config.h"
#include "measure.h"
#include "relays.h"
#include "adc_ltc2500.h"
#include "cal.h"

// ------------------------------------------------------------
// Sample accumulator (also used by cal via measure_integrate)
// ------------------------------------------------------------
struct Accum {
  double   sum = 0.0;
  uint32_t n   = 0;
  void reset()          { sum = 0.0; n = 0; }
  void add(double v)    { sum += v; n++; }
  double mean() const   { return n ? sum / n : NAN; }
};

static Accum    s_acc;          // engine accumulator
static double   s_mean_a = NAN; // chop phase-A mean (ADC volts)
static uint32_t s_phase_t0 = 0; // phase entry time
static uint32_t s_last_az_ms = 0;

// standalone integrator for cal.cpp
static Accum    s_cal_acc;
static uint32_t s_cal_t0 = 0;
static bool     s_cal_active = false;

// latest ADC sample, shared per tick
static float s_adc_v = 0.0f;
static bool  s_adc_new = false, s_adc_ovl = false;

// ------------------------------------------------------------
static void enter_phase(NvState& s, NvPhase p, uint32_t now) {
  s.phase = p;
  s_phase_t0 = now;
  s_acc.reset();
}

// Post-switch settle: SSR chop flips need only a short blank; any
// mechanical relay motion (mode/short/cal, or a range change that
// restarted the phase) uses the full settle time.
static bool settled(const NvState& s, uint32_t now, bool chop_flip) {
  uint32_t need = chop_flip ? CHOP_BLANK_MS : s.settle_ms;
  return !relays_busy() && (now - s_phase_t0 >= need);
}
static bool s_chop_flip_settle = false;   // current settle is an SSR flip

static void apply_autorange(NvState& s, double v) {
  if (!s.autorange) return;
  double a = fabs(v);
  if (s.range == RANGE_20MV && a > AUTORANGE_UP_FRAC * RANGE_20MV_FS)
    measure_set_range(s, RANGE_200MV);
  else if (s.range == RANGE_200MV && a < AUTORANGE_DN_FRAC * RANGE_200MV_FS)
    measure_set_range(s, RANGE_20MV);
}

static void finish_reading(NvState& s, double v, uint32_t now) {
  s.reading_v  = v;
  s.reading_ms = now;
  s.stats.add(v);
  apply_autorange(s, v);
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------
void measure_init(NvState& s) {
  s.phase = PH_IDLE;
  s.live_zero_v = s.cal.zero_v[s.mode][s.range];
  relays_range(s.range == RANGE_200MV);
}

void measure_start(NvState& s) {
  if (s.cal_busy) return;
  s.running = true;
  s.stats.reset();
  s.overload = false;
  s.live_zero_v = s.cal.zero_v[s.mode][s.range];
  relays_request(RLY_K3_SHORT, false);
  relays_request(RLY_K4_CAL,   false);
  s.shorted = false;  s.cal_res_in = false;
  relays_chop(false);                        // forward
  s.chop_rev = false;
  s_chop_flip_settle = false;
  if (s.mode == MODE_LOWZ_CHOP) {
    relays_direct(false);                    // source -> SSR chop bridge
  } else {
    relays_direct(true);                     // source -> direct high-Z
    s_last_az_ms = 0;                        // force an AZ first
  }
  enter_phase(s, PH_SETTLE, millis());
}

void measure_stop(NvState& s) {
  s.running = false;
  enter_phase(s, PH_IDLE, millis());
}

void measure_set_range(NvState& s, NvRange r) {
  if (s.range == r) return;
  s.range = r;
  relays_range(r == RANGE_200MV);
  s.live_zero_v = s.cal.zero_v[s.mode][s.range];
  s_chop_flip_settle = false;
  if (s.running) enter_phase(s, PH_SETTLE, millis());
}

void measure_trigger_az(NvState& s) {
  if (s.running && s.mode == MODE_HIGHZ_AZ && s.phase != PH_CAL)
    s_last_az_ms = 0;
}

// ------------------------------------------------------------
// Cal-owned integration helper
// ------------------------------------------------------------
bool measure_integrate(uint32_t ms, uint32_t now, double* mean_out) {
  if (!s_cal_active) {
    s_cal_acc.reset();
    s_cal_t0 = now;
    s_cal_active = true;
  }
  if (s_adc_new && !s_adc_ovl) s_cal_acc.add(s_adc_v);
  if (now - s_cal_t0 >= ms && s_cal_acc.n > 0) {
    *mean_out = s_cal_acc.mean();
    s_cal_active = false;
    return true;
  }
  return false;
}
void measure_integrate_abort() { s_cal_active = false; }

// ------------------------------------------------------------
// Engine tick
// ------------------------------------------------------------
void measure_tick(NvState& s, uint32_t now) {
  // one ADC poll per tick, result shared with cal integrator
  s_adc_new = adc_poll(&s_adc_v, &s_adc_ovl);
  if (s_adc_new) {
    s.adc_v = s_adc_v;
    if (s_adc_ovl) s.overload = true;
  }

  if (s.cal_busy) { cal_tick(s, now); return; }
  if (!s.running) return;

  // overload: abandon the current integration, restart after settle
  if (s_adc_new && s_adc_ovl && s.phase != PH_IDLE && s.phase != PH_SETTLE) {
    if (s.autorange && s.range == RANGE_20MV) measure_set_range(s, RANGE_200MV);
    enter_phase(s, PH_SETTLE, now);
    return;
  }
  if (s_adc_new && !s_adc_ovl) s.overload = false;

  const bool chop = (s.mode == MODE_LOWZ_CHOP);
  const float g = nv_gain(s);

  switch (s.phase) {

    case PH_IDLE:
      break;

    case PH_SETTLE:
      if (!settled(s, now, s_chop_flip_settle)) break;
      s_chop_flip_settle = false;
      if (chop) {
        enter_phase(s, s.chop_rev ? PH_INT_B : PH_INT_A, now);
      } else if (s.az_interval_s &&
                 (s_last_az_ms == 0 ||
                  now - s_last_az_ms >= (uint32_t)s.az_interval_s * 1000UL)) {
        relays_request(RLY_K3_SHORT, true);
        s.shorted = true;
        enter_phase(s, PH_AZ_SETTLE, now);
      } else {
        enter_phase(s, PH_INT_DC, now);
      }
      break;

    // ---- low-Z chop: A (straight) / B (reversed) halves ------
    case PH_INT_A:
    case PH_INT_B: {
      if (s_adc_new && !s_adc_ovl) s_acc.add(s_adc_v);
      uint32_t integ_ms = (s.chop_half_ms > CHOP_BLANK_MS)
                            ? (uint32_t)(s.chop_half_ms - CHOP_BLANK_MS) : 1;
      if (now - s_phase_t0 < integ_ms || s_acc.n == 0) break;
      if (s.phase == PH_INT_A) {
        s_mean_a = s_acc.mean();
      } else if (!isnan(s_mean_a)) {
        // demodulate: offset and its drift cancel in the difference
        finish_reading(s, (s_mean_a - s_acc.mean()) / (2.0 * g), now);
      }
      s.chop_rev = !s.chop_rev;
      relays_chop(s.chop_rev);               // SSR flip: microseconds
      s_chop_flip_settle = true;
      enter_phase(s, PH_SETTLE, now);
      break;
    }

    // ---- high-Z DC with periodic auto-zero -------------------
    case PH_INT_DC:
      if (s_adc_new && !s_adc_ovl) s_acc.add(s_adc_v);
      if (now - s_phase_t0 < s.aperture_ms || s_acc.n == 0) break;
      finish_reading(s, s_acc.mean() / g - s.live_zero_v, now);
      enter_phase(s, PH_SETTLE, now);
      break;

    case PH_AZ_SETTLE:
      if (!settled(s, now, false)) break;
      enter_phase(s, PH_AZ_INT, now);
      break;

    case PH_AZ_INT:
      if (s_adc_new && !s_adc_ovl) s_acc.add(s_adc_v);
      if (now - s_phase_t0 < s.aperture_ms || s_acc.n == 0) break;
      s.live_zero_v = s_acc.mean() / g;
      s_last_az_ms  = now;
      relays_request(RLY_K3_SHORT, false);
      s.shorted = false;
      enter_phase(s, PH_SETTLE, now);
      break;

    case PH_CAL:   // owned by cal.cpp (cal_busy path above)
      break;
  }

  s.az_age_s = s_last_az_ms ? (now - s_last_az_ms) / 1000UL : 0;
}
