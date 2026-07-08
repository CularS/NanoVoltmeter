#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "adc_ltc2500.h"

static const SPISettings SPI_ADC(4000000, MSBFIRST, SPI_MODE0);

static IntervalTimer  s_mclk_timer;
static volatile uint32_t s_conv_count = 0;   // MCLK pulses issued
static volatile bool  s_pending = false;     // filtered result awaiting read
static volatile uint32_t s_edge_us = 0;      // micros() of last MCLK edge
static uint32_t       s_sample_count = 0;
static bool           s_demo = false;
static volatile float s_demo_in = 0.0f;      // demo: volts at ADC input

// MCLK ISR: rising edge starts a conversion (t_conv ~660 ns, then the
// part idles until the next MCLK). Every ADC_DF conversions the sinc
// filter produces a new output word on interface A.
static void mclk_isr() {
  digitalWriteFast(PIN_ADC_MCLK, HIGH);
  delayNanoseconds(50);
  digitalWriteFast(PIN_ADC_MCLK, LOW);
  s_edge_us = micros();
  uint32_t n = ++s_conv_count;
  if (n % ADC_DF == 0) s_pending = true;
}

// Program the 12-bit control word (LTC2500-32 DS Fig.31/Table 3):
//   C[11:10]=10 (valid), C[9]=DGC off, C[8]=DGE off,
//   C[7:4]=DF code, C[3:0]=0100 (sinc4)
// A transaction window is open at power-up; the word must arrive in
// the first 12 SCKA clocks. We send 16 clocks with the word left-
// aligned — clocks 13..16 fall after the window closes (harmless).
// During normal reads MOSI idles low -> C[11:10]=00 = invalid word,
// so the configuration can never be reprogrammed by accident.
static uint8_t df_code() {
  switch (ADC_DF) {
    case 4: return 0x2;   case 8: return 0x3;   case 16: return 0x4;
    case 32: return 0x5;  case 64: return 0x6;  case 128: return 0x7;
    case 256: return 0x8; case 512: return 0x9; case 1024: return 0xA;
    case 2048: return 0xB; case 4096: return 0xC; case 8192: return 0xD;
    case 16384: return 0xE;
  }
  return 0x8;  // 256
}

static void write_config() {
  uint16_t word12 = (uint16_t)((0x2u << 10) | (df_code() << 4) | 0x4u);
  uint16_t tx = (uint16_t)(word12 << 4);     // left-align in 16 clocks
  SPI.beginTransaction(SPI_ADC);
  digitalWrite(PIN_ADC_CS, LOW);             // RDLA falling edge
  SPI.transfer16(tx);
  digitalWrite(PIN_ADC_CS, HIGH);
  SPI.endTransaction();
}

void adc_init(bool demo) {
  s_demo = demo;
  pinMode(PIN_ADC_CS, OUTPUT);
  digitalWrite(PIN_ADC_CS, HIGH);
  pinMode(PIN_ADC_MCLK, OUTPUT);
  digitalWrite(PIN_ADC_MCLK, LOW);
  pinMode(PIN_ADC_BUSY, INPUT);

  if (!s_demo) {
    delay(2);                 // power-up transaction window
    write_config();           // sinc4, DF=ADC_DF, DGC/DGE off
  }
  s_conv_count = 0;
  s_pending = false;
  s_mclk_timer.begin(mclk_isr, 1000000.0f / ADC_MCLK_HZ);
}

static int32_t read_filtered_word() {
  SPI.beginTransaction(SPI_ADC);
  digitalWrite(PIN_ADC_CS, LOW);
  uint32_t w = 0;
  for (int i = 0; i < 4; i++) w = (w << 8) | SPI.transfer(0x00);
  digitalWrite(PIN_ADC_CS, HIGH);
  SPI.endTransaction();
  return (int32_t)w;
}

bool adc_poll(float* volts, bool* overload) {
  if (!s_pending) return false;

  int32_t code;
  if (s_demo) {
    s_pending = false;
    float noisy = s_demo_in +
                  1e-5f * ((float)random(-32768, 32768) / 32768.0f);
    double c = (double)noisy / ADC_VREF * ADC_FS_CODE;
    if (c >  ADC_FS_CODE - 1) c =  ADC_FS_CODE - 1;
    if (c < -ADC_FS_CODE)     c = -ADC_FS_CODE;
    code = (int32_t)c;
  } else {
    // Read only in the quiet part of the conversion cycle: BUSY low
    // (conversion done) and early enough after the last MCLK edge
    // that the 32-bit read (~8us at 4MHz) finishes before the next
    // conversion starts. Otherwise retry on the next loop pass —
    // the result register holds until the next DF boundary.
    uint32_t since = micros() - s_edge_us;
    if (digitalReadFast(PIN_ADC_BUSY) || since < 2 ||
        since > (1000000UL / ADC_MCLK_HZ) - 15)
      return false;
    s_pending = false;
    code = read_filtered_word();
  }
  s_sample_count++;

  *volts    = (float)((double)code / ADC_FS_CODE * ADC_VREF);
  *overload = fabsf(*volts) > ADC_OVERLOAD_FRAC * ADC_VREF;
  return true;
}

uint32_t adc_sample_count() { return s_sample_count; }

void adc_demo_set_input(float v) { s_demo_in = v; }
