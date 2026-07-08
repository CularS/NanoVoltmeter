#include <Arduino.h>
#include <EEPROM.h>
#include "nv_settings.h"

static void defaults(NvSettings& s) {
  s.magic = NV_MAGIC;
  s.use_dhcp = NV_DEFAULT_DHCP;
  uint8_t ip[4] = NV_DEFAULT_IP, sub[4] = NV_DEFAULT_SUBNET, gw[4] = NV_DEFAULT_GATEWAY;
  memcpy(s.static_ip, ip, 4);
  memcpy(s.subnet, sub, 4);
  memcpy(s.gateway, gw, 4);
  strncpy(s.hostname, NV_DEFAULT_HOSTNAME, sizeof(s.hostname));
  s.demo_mode    = DEMO_MODE_DEFAULT;
  s.range        = RANGE_20MV;
  s.autorange    = false;
  s.mode         = MODE_LOWZ_CHOP;
  s.chop_half_ms = DEF_CHOP_HALF_MS;
  s.settle_ms    = DEF_SETTLE_MS;
  s.aperture_ms  = DEF_APERTURE_MS;
  s.az_interval_s= DEF_AZ_INTERVAL_S;
  s.backlight    = DEF_BACKLIGHT;
}

void nv_settings_load(NvSettings& s) {
  EEPROM.get(NV_EEPROM_ADDR, s);
  if (s.magic != NV_MAGIC) {
    defaults(s);
    nv_settings_save(s);
  }
  // clamp against corruption
  if (s.chop_half_ms < 5 || s.chop_half_ms > 10000) s.chop_half_ms = DEF_CHOP_HALF_MS;
  if (s.settle_ms    < 20  || s.settle_ms    > 5000)  s.settle_ms    = DEF_SETTLE_MS;
  if (s.aperture_ms  < 50  || s.aperture_ms  > 60000) s.aperture_ms  = DEF_APERTURE_MS;
  if (s.az_interval_s > 3600) s.az_interval_s = DEF_AZ_INTERVAL_S;
  if (s.range > 1) s.range = RANGE_20MV;
  if (s.mode  > 1) s.mode  = MODE_LOWZ_CHOP;
  if (s.backlight < BL_LEVEL_MIN || s.backlight > BL_LEVEL_MAX)
    s.backlight = DEF_BACKLIGHT;
  s.hostname[sizeof(s.hostname) - 1] = '\0';
}

void nv_settings_save(const NvSettings& s) { EEPROM.put(NV_EEPROM_ADDR, s); }

bool nv_parse_ip(const char* str, uint8_t ip[4]) {
  int a, b, c, d;
  if (sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  if ((a | b | c | d) & ~0xFF) return false;
  ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
  return true;
}

void nv_fmt_ip(const uint8_t ip[4], char* buf) {
  sprintf(buf, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

void nv_settings_to_state(const NvSettings& ns, NvState& s) {
  s.range         = (NvRange)ns.range;
  s.autorange     = ns.autorange;
  s.mode          = (NvMode)ns.mode;
  s.chop_half_ms  = ns.chop_half_ms;
  s.settle_ms     = ns.settle_ms;
  s.aperture_ms   = ns.aperture_ms;
  s.az_interval_s = ns.az_interval_s;
  s.backlight     = ns.backlight;
  s.demo_mode     = ns.demo_mode;
}

void nv_state_to_settings(const NvState& s, NvSettings& ns) {
  ns.range         = (uint8_t)s.range;
  ns.autorange     = s.autorange;
  ns.mode          = (uint8_t)s.mode;
  ns.chop_half_ms  = s.chop_half_ms;
  ns.settle_ms     = s.settle_ms;
  ns.aperture_ms   = s.aperture_ms;
  ns.az_interval_s = s.az_interval_s;
  ns.backlight     = s.backlight;
  ns.demo_mode     = s.demo_mode;
}
