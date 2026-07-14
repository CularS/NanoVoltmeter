#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "cal.h"
#include "measure.h"
#include "relays.h"
#include "dac_ad5689r.h"

#define CAL_MAGIC       0x4E564342UL   // 'N','V','C','B' — bumped: bipolar bias servo
#define CAL_EEPROM_ADDR 256            // after NvSettings block

// ------------------------------------------------------------
// EEPROM store
// ------------------------------------------------------------
void cal_defaults(NvCal& c) {
  c.magic = CAL_MAGIC;
  c.gain_corr[0] = c.gain_corr[1] = 1.0f;
  for (int m = 0; m < 2; m++)
    for (int r = 0; r < 2; r++) c.zero_v[m][r] = 0.0f;
  c.bias_pa = 0.0f;
  c.ib_pa = 0.0f;
  c.cal_time_s = 0;
}

void cal_load(NvCal& c) {
  EEPROM.get(CAL_EEPROM_ADDR, c);
  bool ok = (c.magic == CAL_MAGIC) &&
            isfinite(c.gain_corr[0]) && c.gain_corr[0] > 0.9f && c.gain_corr[0] < 1.1f &&
            isfinite(c.gain_corr[1]) && c.gain_corr[1] > 0.9f && c.gain_corr[1] < 1.1f;
  if (!ok) { cal_defaults(c); cal_save(c); }
}

void cal_save(const NvCal& c) { EEPROM.put(CAL_EEPROM_ADDR, c); }

// ------------------------------------------------------------
// Routine state machines
// ------------------------------------------------------------
enum CalKind : uint8_t { CK_NONE = 0, CK_ZERO, CK_BIAS };
enum CalStep : uint8_t {
  CS_RELAY, CS_SETTLE, CS_MEASURE,          // generic
  CS_BIAS_STEP,                              // bias servo iterate
  CS_RESTORE
};

static CalKind  s_kind = CK_NONE;
static CalStep  s_step = CS_RELAY;
static uint32_t s_t0 = 0;
static int      s_iter = 0;
static double   s_meas = 0.0;
static float    s_pa_lo = 0, s_pa_hi = 0;   // bias bisection bounds (pA)

static void msg(NvState& s, const char* m) {
  strncpy(s.cal_msg, m, sizeof(s.cal_msg) - 1);
  s.cal_msg[sizeof(s.cal_msg) - 1] = '\0';
}

static bool begin_cal(NvState& s, CalKind k) {
  if (s.cal_busy) return false;
  measure_stop(s);
  measure_integrate_abort();
  s.cal_busy = true;
  s.phase = PH_CAL;
  s_kind = k;
  s_step = CS_RELAY;
  s_iter = 0;
  return true;
}

bool cal_start_zero(NvState& s) {
  if (!begin_cal(s, CK_ZERO)) return false;
  msg(s, "zero: shorting input");
  relays_request(RLY_K4_CAL, false);
  relays_chop(false);
  relays_request(RLY_K3_SHORT, true);
  s.shorted = true; s.cal_res_in = false;
  return true;
}

bool cal_start_bias(NvState& s) {
  if (!begin_cal(s, CK_BIAS)) return false;
  msg(s, "bias: 100k across input");
  // 20 mV range for resolution; input open except the 100k standard.
  measure_set_range(s, RANGE_20MV);
  relays_chop(false);
  relays_direct(true);                    // clean direct path for pA work
  relays_request(RLY_K3_SHORT, false);
  relays_request(RLY_K4_CAL, true);
  s.shorted = false; s.cal_res_in = true;
  s_pa_lo = -BIAS_NEG_FULL_PA;
  s_pa_hi =  BIAS_POS_FULL_PA;
  bias_set_pa((s_pa_lo + s_pa_hi) / 2);
  return true;
}

bool cal_gain_from_expected(NvState& s, double expected_v) {
  // Uses the live reading: apply the reference source, let readings
  // settle, then 'calgain <value>' captures the correction.
  if (s.cal_busy || isnan(s.reading_v) || fabs(s.reading_v) < 1e-6) return false;
  if (expected_v == 0.0) return false;
  float corr = s.cal.gain_corr[s.range] * (float)(s.reading_v / expected_v);
  if (corr < 0.9f || corr > 1.1f) return false;   // implausible — refuse
  s.cal.gain_corr[s.range] = corr;
  s.cal.cal_time_s = millis() / 1000UL;
  cal_save(s.cal);
  return true;
}

void cal_abort(NvState& s) {
  if (!s.cal_busy) return;
  measure_integrate_abort();
  relays_request(RLY_K3_SHORT, false);
  relays_request(RLY_K4_CAL, false);
  s.shorted = false; s.cal_res_in = false;
  bias_set_pa(s.cal.bias_pa);
  s.cal_busy = false;
  s.phase = PH_IDLE;
  s_kind = CK_NONE;
  msg(s, "aborted");
}

// ------------------------------------------------------------
static void finish(NvState& s, const char* m) {
  relays_request(RLY_K3_SHORT, false);
  relays_request(RLY_K4_CAL, false);
  s.shorted = false; s.cal_res_in = false;
  s.cal.cal_time_s = millis() / 1000UL;
  cal_save(s.cal);
  s.cal_busy = false;
  s.phase = PH_IDLE;
  s_kind = CK_NONE;
  msg(s, m);
}

void cal_tick(NvState& s, uint32_t now) {
  if (s_kind == CK_NONE) return;
  const float g = nv_gain(s);

  switch (s_step) {

    case CS_RELAY:
      if (relays_busy()) break;
      s_t0 = now;
      s_step = CS_SETTLE;
      break;

    case CS_SETTLE: {
      // bias path (10G into the RC filter) needs seconds; zero needs less
      uint32_t wait = (s_kind == CK_BIAS) ? BIASCAL_SETTLE_MS : 500;
      if (now - s_t0 < wait) break;
      s_step = CS_MEASURE;
      break;
    }

    case CS_MEASURE:
      if (!measure_integrate(2000, now, &s_meas)) break;
      if (s_kind == CK_ZERO) {
        s.cal.zero_v[s.mode][s.range] = (float)(s_meas / g);
        s.live_zero_v = s.cal.zero_v[s.mode][s.range];
        finish(s, "zero: stored");
      } else {
        s_step = CS_BIAS_STEP;
      }
      break;

    case CS_BIAS_STEP: {
      // Node current = I_leak + I_servo; V(100k) = I_net x 100k.
      // Leakage sources current INTO the node (v > 0 uncompensated),
      // so v > 0 -> servo must sink more -> move NEGATIVE.
      double v_in = s_meas / g;
      char buf[48];
      snprintf(buf, sizeof(buf), "bias it%d: %+.0f nV", s_iter, v_in * 1e9);
      msg(s, buf);
      if (fabs(v_in) < BIASCAL_TOL_V || s_iter >= BIASCAL_MAX_ITER ||
          s_pa_hi - s_pa_lo < 0.05f) {
        s.cal.bias_pa = bias_get_pa();
        s.cal.ib_pa = -s.cal.bias_pa;      // leakage = -servo at null
        finish(s, "bias: nulled + stored");
        break;
      }
      float cur = bias_get_pa();
      if (v_in > 0) s_pa_hi = cur; else s_pa_lo = cur;
      bias_set_pa((s_pa_lo + s_pa_hi) / 2);
      s_iter++;
      s_t0 = now;
      s_step = CS_SETTLE;
      break;
    }

    case CS_RESTORE:
      break;
  }
}
