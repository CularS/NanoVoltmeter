#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "dac_ad5689r.h"

// AD5689R frame: 24 bits = command[4] address[4] data[16], MSB first.
// Command 0x3 = write & update. Address 0x1 = DAC A, 0x8 = DAC B.
// SPI mode 1 (CPOL=0, CPHA=1), SYNC framing each 24-bit word.  (VERIFY)

static const SPISettings SPI_DAC(1000000, MSBFIRST, SPI_MODE1);
static float s_bias_pa = 0.0f;

static void xfer24(uint8_t cmd, uint8_t addr, uint16_t data) {
  SPI.beginTransaction(SPI_DAC);
  digitalWrite(PIN_DAC_CS, LOW);
  SPI.transfer((uint8_t)((cmd << 4) | addr));
  SPI.transfer((uint8_t)(data >> 8));
  SPI.transfer((uint8_t)(data & 0xFF));
  digitalWrite(PIN_DAC_CS, HIGH);
  SPI.endTransaction();
}

void ad5689r_init() {
  pinMode(PIN_DAC_CS, OUTPUT);
  digitalWrite(PIN_DAC_CS, HIGH);
  pinMode(PIN_DAC_RST, OUTPUT);
  digitalWrite(PIN_DAC_RST, LOW);
  delay(1);
  digitalWrite(PIN_DAC_RST, HIGH);
  delay(1);
  xfer24(0x4, 0x0, 0x0000);      // both channels normal operation
  bias_set_pa(0.0f);
}

void ad5689r_write_a(uint16_t code) { xfer24(0x3, 0x1, code); }
void ad5689r_write_b(uint16_t code) { xfer24(0x3, 0x8, code); }

void bias_set_pa(float pa) {
  if (pa < -BIAS_NEG_FULL_PA) pa = -BIAS_NEG_FULL_PA;
  if (pa >  BIAS_POS_FULL_PA) pa =  BIAS_POS_FULL_PA;
  s_bias_pa = pa;
  if (pa <= 0.0f) {
    ad5689r_write_a((uint16_t)(-pa / BIAS_NEG_FULL_PA * 65535.0f + 0.5f));
    ad5689r_write_b(0);
  } else {
    ad5689r_write_a(0);
    ad5689r_write_b((uint16_t)(pa / BIAS_POS_FULL_PA * 65535.0f + 0.5f));
  }
}

float bias_get_pa() { return s_bias_pa; }
