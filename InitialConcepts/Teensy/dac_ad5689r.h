#pragma once
#include <stdint.h>

// AD5689R dual 16-bit DAC — bipolar bias-current servo.
// The JFET gate leakage SOURCES current into the gate node, so the
// servo must primarily SINK:
//   ch A -> inverting stage (x-2, U2B) -> 10G : 0 .. -1000 pA (sink)
//   ch B -> direct -> 10G                     : 0 ..  +500 pA (source)
// bias_set_pa() drives one channel at a time, the other at zero.

void     ad5689r_init();
void     ad5689r_write_a(uint16_t code);
void     ad5689r_write_b(uint16_t code);

void     bias_set_pa(float pa);     // clamped to [-1000, +500]
float    bias_get_pa();
