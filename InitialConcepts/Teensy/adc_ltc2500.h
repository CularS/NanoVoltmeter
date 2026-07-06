#pragma once
#include <stdint.h>

// ============================================================
// LTC2500-32 driver — filtered (sinc) output path.
// MCLK is strobed by an IntervalTimer at ADC_MCLK_HZ; every
// ADC_DF conversions the filtered 32-bit result is ready and
// read over SPI. adc_poll() returns true once per new sample.
//
// In demo mode the same interface delivers simulated samples so
// the measurement engine / UI / cal code never know the
// difference — that is the debug seam for the whole instrument.
// ============================================================

void adc_init(bool demo);
// True when a new filtered sample is available; *volts = ADC-input volts
// (VOUT - STAR at the front end, i.e. after the preamp gain).
bool adc_poll(float* volts, bool* overload);
uint32_t adc_sample_count();      // total filtered samples since boot

// Demo-side knobs (called by demo.cpp to shape the simulation)
void adc_demo_set_input(float volts_at_adc);
