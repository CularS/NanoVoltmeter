#pragma once

// ============================================================
// Nanovoltmeter controller — Teensy 4.1
//
// Front end: see Nanovoltmeter/nanovoltmeter_fe.kicad_sch
//   K1 chop relay (polarity reversal), K2 range (G=201 / G=21),
//   K3 input short (auto-zero / blanking), K4 100k bias-cal std,
//   AD5689R bias-current servo DAC, LTC2500-32 ADC on VOUT.
//
// All relay coils are driven through digital isolators + ULN2003
// on the digital side (J3 of the front end). ADC/DAC SPI crosses
// the same isolation barrier.
//
// DEMO MODE (runtime, EEPROM-persisted, 'demo on'/'demo off'):
// full firmware runs against a simulated front end — UI, network,
// measurement engine and cal routines all testable with only a
// Teensy + TFT on the bench.
// ============================================================

#define DEMO_MODE_DEFAULT   1   // first-boot default: 1 = demo on

// ------------------------------------------------------------
// Pin map — Teensy 4.1
// SPI0 (11/12/13): LTC2500-32 ADC + AD5689R DAC (isolated bus)
// SPI1 (26/27):    ILI9341 TFT (same wiring as dual-PDH build)
// ------------------------------------------------------------

// --- SPI0 (measurement bus) ---------------------------------
#define PIN_MOSI          11
#define PIN_MISO          12
#define PIN_SCK           13

#define PIN_ADC_CS        10   // LTC2500-32 chip select
#define PIN_ADC_BUSY       9   // LTC2500-32 BUSY/DRDY (high while converting)
#define PIN_ADC_MCLK       5   // conversion strobe (IntervalTimer-driven)

#define PIN_DAC_CS         8   // AD5689R SYNC
#define PIN_DAC_RST        4   // AD5689R /RESET (drive HIGH for normal run)

// --- Switch drives (via isolators + TBD62083, active HIGH) ------
// SSR LEVEL lines (PhotoMOS chop bridge + range select on the FE;
// Teensy HIGH -> driver sinks the pulled-up FE line LOW):
#define PIN_CHOP           2   // HIGH = reversed polarity, LOW = forward
#define PIN_RANGE          3   // HIGH = G=21 (200 mV), LOW = G=201 (20 mV)
// Latching 2-coil mechanical relays: pulse SET or RESET, never hold.
#define PIN_K3_SET        24   // short: SET = input shorted
#define PIN_K3_RST        25
#define PIN_K4_SET        33   // cal:   SET = 100k across input
#define PIN_K4_RST        34
#define PIN_K5_SET        40   // mode:  SET = SSR chop bridge in path (low-Z)
#define PIN_K5_RST        41   //        RESET = source direct (high-Z)

// (No front-panel buttons — the UI is driven entirely by the
//  XPT2046 touchscreen and the web dashboard.)

// --- TFT backlight: 4 switched resistor branches ------------
// P-MOSFET (DMG3415U) high-side switches, gate LOW = branch ON.
// 100k gate pull-ups -> all branches off at boot = trickle level
// through the always-on 330R. Branch resistors 150R/68R/33R/15R
// give 5 cumulative (thermometer-coded) DC brightness levels:
// ~1.2 / 4 / 10 / 22 / 48 mA. No PWM — pure DC, quiet.
#define PIN_BL_1          35   // +150R  (level >= 2)
#define PIN_BL_2          36   // +68R   (level >= 3)
#define PIN_BL_3          37   // +33R   (level >= 4)
#define PIN_BL_4          38   // +15R   (level >= 5)
#define BL_LEVEL_MIN       1
#define BL_LEVEL_MAX       5
#define DEF_BACKLIGHT      3

// --- SPI1 (display) — identical to dual-PDH controller ------
#define PIN_TFT_MOSI      26
#define PIN_TFT_SCK       27
#define PIN_TFT_CS        28
#define PIN_TFT_DC        29
#define PIN_TFT_RST       30
// XPT2046 resistive touch, shared SPI1. T_DO must be wired to
// pin 39 (SPI1 MISO) — the TFT itself never drives MISO.
#define PIN_TOUCH_CS      31
#define PIN_TOUCH_IRQ     32

// Touch calibration — raw ADC extents mapped to the 320x240
// landscape screen. Tune with the 'touch' CLI command at
// bring-up (tap corners, adjust until mapped x/y match).
#define TS_RAW_MIN       250
#define TS_RAW_MAX      3850
#define TS_SWAP_XY         1   // raw axes are portrait on most modules
#define TS_FLIP_X          0
#define TS_FLIP_Y          1
#define TS_DEBOUNCE_MS   250   // min interval between accepted taps

// ------------------------------------------------------------
// Relay timing
// ------------------------------------------------------------
#define RELAY_PULSE_MS      10   // latching coil pulse width
#define RELAY_GAP_MS        10   // gap between queued coil pulses

// ------------------------------------------------------------
// ADC — LTC2500-32, sinc filter, line-synchronous rates
// MCLK 15360 Hz / DF 256  -> 60 filtered samples/s, nulls at n*60 Hz.
// For 50 Hz mains use 12800 Hz / DF 256 -> 50 SPS.       (VERIFY vs datasheet)
// ------------------------------------------------------------
#define ADC_MCLK_HZ       15360
#define ADC_DF              256
#define ADC_VREF            5.0f     // LTC2500 REF pin (from front-end reference)
#define ADC_FS_CODE         (2147483648.0)  // 2^31 (32-bit signed full scale)
#define ADC_OVERLOAD_FRAC   0.95f    // |code| above this fraction of FS = overload

// ------------------------------------------------------------
// Front-end constants (nominal; corrected by cal factors)
// ------------------------------------------------------------
#define GAIN_20MV_NOM     201.0f     // K2 RESET: 2x1k over 10R
#define GAIN_200MV_NOM     21.0f     // K2 SET:   2x1k over 100R
#define RANGE_20MV_FS     0.020f     // volts, nominal input full scale
#define RANGE_200MV_FS    0.200f

#define BIAS_DAC_BITS     16
// Bipolar servo: DAC A -> x-2 inverter -> 10G sinks up to 1 nA
// (gate leakage sources current INTO the node); DAC B -> 10G
// sources up to +500 pA for the opposite-polarity case.
#define BIAS_NEG_FULL_PA 1000.0f
#define BIAS_POS_FULL_PA  500.0f
#define CAL_RES_OHM       100e3f     // K4 bias-cal standard (Y0007 100k)

// ------------------------------------------------------------
// Measurement engine defaults (all runtime-adjustable, persisted)
// SSR chop switches in us, so fast chop (>= LSK389 1/f corner)
// is available: 20 ms half = 25 Hz full cycle.
// ------------------------------------------------------------
#define DEF_CHOP_HALF_MS    20       // chop half-period (25 Hz full cycle)
#define CHOP_BLANK_MS        4       // discard after an SSR chop flip
#define DEF_SETTLE_MS      100       // discard after a mechanical relay change
#define DEF_APERTURE_MS   1000       // high-Z mode integration per reading
#define DEF_AZ_INTERVAL_S   30       // high-Z auto-zero spacing (0 = off)
#define AUTORANGE_UP_FRAC  0.90f     // uprange above this fraction of FS
#define AUTORANGE_DN_FRAC  0.08f     // downrange below this fraction of FS

// Bias-cal servo
#define BIASCAL_TOL_V      3e-7f     // stop when |V(100k)| < 300 nV (3 pA)
#define BIASCAL_MAX_ITER   24
#define BIASCAL_SETTLE_MS  3000      // 10G x filter needs seconds to settle

// ------------------------------------------------------------
// Timing
// ------------------------------------------------------------
#define CONTROL_PERIOD_US 1000UL     // 1 kHz engine tick
#define DISPLAY_PERIOD_MS  100UL     // 10 Hz UI + network poll
#define DEBUG_PERIOD_MS   2000UL
