#pragma once
#include "nv_state.h"

// ============================================================
// Measurement engine — non-blocking state machine, ticked at
// 1 kHz from loop(). Owns K1 (chop), K3 (short) and K2 (range)
// sequencing during normal running; hands itself to cal.cpp
// while a calibration routine is active (phase == PH_CAL).
// ============================================================

void measure_init(NvState& s);
void measure_tick(NvState& s, uint32_t now_ms);

void measure_start(NvState& s);
void measure_stop(NvState& s);
void measure_set_range(NvState& s, NvRange r);   // switches K2, restarts phase
void measure_trigger_az(NvState& s);             // force an auto-zero now (high-Z)

// Used by cal.cpp: run one integration and get the mean ADC voltage.
// Returns true when done; call every tick with the same args.
bool measure_integrate(uint32_t ms, uint32_t now_ms, double* mean_out);
void measure_integrate_abort();
