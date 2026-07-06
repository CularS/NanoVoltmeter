#pragma once
#include <stdint.h>

// ============================================================
// Switch drivers, two kinds:
//
// LATCHING MECHANICAL (K3 short, K4 cal, K5 mode): 10 ms pulse on
// the SET or RESET coil, queued one at a time. relays_busy() lets
// the engine wait for the armature before starting a settle.
//
// SOLID-STATE LEVELS (chop bridge, range select): PhotoMOS LED
// drives, switched in microseconds by a GPIO level — no queue, no
// busy state, no wear. Chop can therefore run to ~100 Hz.
// ============================================================

enum RelayId : uint8_t { RLY_K3_SHORT = 0, RLY_K4_CAL, RLY_K5_BRIDGE };

void relays_init();
void relays_request(RelayId k, bool set, bool force = false);
bool relays_get(RelayId k);          // last commanded state
bool relays_busy();                  // coil pulses pending or in flight
void relays_tick(uint32_t now_ms);   // call every loop pass

// SSR level controls (instantaneous)
void relays_chop(bool reversed);     // chop bridge polarity
bool relays_chop_get();
void relays_range(bool g21_200mv);   // true = G=21 (200 mV range)
bool relays_range_get();
