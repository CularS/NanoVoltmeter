#include <Arduino.h>
#include "config.h"
#include "demo.h"
#include "adc_ltc2500.h"
#include "dac_ad5689r.h"
#include "relays.h"

// "True" hidden values the firmware is supposed to discover
static const float IB_TRUE_PA    = 62.0f;    // actual input bias current
static const float SRC_EMF_V     = 8.2e-3f;  // TVC output EMF
static const float SRC_R_OHM     = 90.0f;    // source resistance
static const float AMP_OFFSET_V  = 1.7e-6f;  // input-referred amp offset

void demo_init() { randomSeed(0x5EED); }

void demo_tick(const NvState& s, uint32_t now) {
  float t = now * 1e-3f;

  // Source EMF with slow thermal drift (~100 nV over minutes)
  float v_src = SRC_EMF_V + 1.0e-7f * sinf(t * 0.011f);

  // Net node current: leakage sources INTO the node, servo adds
  // algebraically (negative servo sinks -> nulls the leakage)
  float ib_net_pa = IB_TRUE_PA + bias_get_pa();

  // What is present at the amplifier input, per relay state:
  float v_in;
  if (relays_get(RLY_K3_SHORT)) {
    v_in = 0.0f;                                     // shorted at the block
  } else if (relays_get(RLY_K4_CAL)) {
    v_in = ib_net_pa * 1e-12f * CAL_RES_OHM;         // Ib x 100k, input open
  } else {
    v_in = v_src + ib_net_pa * 1e-12f * SRC_R_OHM;   // normal measurement
  }

  if (relays_chop_get()) v_in = -v_in;               // SSR polarity reversal

  // Amplifier offset (input-referred) with slow 1/f-ish drift —
  // the chop demod should remove this completely.
  float off = AMP_OFFSET_V + 3.0e-7f * sinf(t * 0.037f + 1.0f);

  float gain = (s.range == RANGE_20MV) ? GAIN_20MV_NOM : GAIN_200MV_NOM;
  adc_demo_set_input(gain * (v_in + off));
}
