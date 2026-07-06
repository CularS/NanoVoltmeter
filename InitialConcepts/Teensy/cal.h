#pragma once
#include "nv_state.h"
#include <Print.h>

// ============================================================
// Calibration: EEPROM-backed constants + non-blocking routines.
//   zero cal : K3 short, measure stored zero for current mode/range
//   bias cal : K3 open, K4 100k in, servo the AD5689R until the
//              I_b x 100k drop nulls; stores DAC code + I_b
//   gain cal : user applies a known source, 'calgain <expected>'
//              computes and stores the gain correction
// Routines own the measurement engine while running (cal_busy).
// ============================================================

void cal_load(NvCal& c);              // EEPROM -> struct (defaults if blank)
void cal_save(const NvCal& c);        // struct -> EEPROM
void cal_defaults(NvCal& c);

bool cal_start_zero(NvState& s);      // returns false if busy/running
bool cal_start_bias(NvState& s);
bool cal_gain_from_expected(NvState& s, double expected_v);  // uses last reading
void cal_abort(NvState& s);
void cal_tick(NvState& s, uint32_t now_ms);
