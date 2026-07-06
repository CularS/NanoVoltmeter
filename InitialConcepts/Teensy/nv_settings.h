#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "nv_state.h"

// ============================================================
// Persisted settings (network + instrument) — EEPROM addr 0.
// Calibration constants live in their own block (see cal.cpp).
// ============================================================

#define NV_MAGIC       0x4E564D32UL   // 'N','V','M','2' — bump on struct change
#define NV_EEPROM_ADDR 0

struct NvSettings {
  uint32_t magic;
  // network
  bool     use_dhcp;
  uint8_t  static_ip[4];
  uint8_t  subnet[4];
  uint8_t  gateway[4];
  char     hostname[32];
  // instrument
  bool     demo_mode;
  uint8_t  range;          // NvRange
  bool     autorange;
  uint8_t  mode;           // NvMode
  uint16_t chop_half_ms;
  uint16_t settle_ms;
  uint16_t aperture_ms;
  uint16_t az_interval_s;
};

#define NV_DEFAULT_DHCP      true
#define NV_DEFAULT_IP        {192, 168, 1, 201}
#define NV_DEFAULT_SUBNET    {255, 255, 255, 0}
#define NV_DEFAULT_GATEWAY   {192, 168, 1, 1}
#define NV_DEFAULT_HOSTNAME  "nanovolt"

void nv_settings_load(NvSettings& s);
void nv_settings_save(const NvSettings& s);
bool nv_parse_ip(const char* str, uint8_t ip[4]);
void nv_fmt_ip(const uint8_t ip[4], char* buf);

// Copy the instrument fields between settings and live state.
void nv_settings_to_state(const NvSettings& ns, NvState& s);
void nv_state_to_settings(const NvState& s, NvSettings& ns);
