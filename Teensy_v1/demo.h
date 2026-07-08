#pragma once
#include "nv_state.h"

// Simulated front end: a slowly drifting ~8 mV thermocouple EMF,
// a drifting amplifier offset, a real input bias current that the
// bias-cal servo can null, and correct response to every relay
// state — so the full firmware (engine, cal, UI, network) can be
// exercised with no front-end hardware attached.

void demo_init();
void demo_tick(const NvState& s, uint32_t now_ms);
