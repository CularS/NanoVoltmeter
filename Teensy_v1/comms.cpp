#include <Arduino.h>
#include "config.h"
#include "comms.h"
#include "measure.h"
#include "cal.h"
#include "relays.h"
#include "dac_ad5689r.h"
#include "adc_ltc2500.h"
#include "nv_settings.h"
#include "display.h"

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
static void print_reading(Print& o, const NvState& s) {
  if (isnan(s.reading_v)) { o.println(F("no reading yet")); return; }
  o.print(s.reading_v * 1e6, 4); o.print(F(" uV   (sigma "));
  if (s.stats.n > 1) { o.print(s.stats.sigma() * 1e9, 1); o.print(F(" nV")); }
  else o.print('-');
  o.print(F(", N=")); o.print(s.stats.n); o.println(')');
}

// Parse "12.3", "12.3mV", "12.3uV", "12.3nV", "0.0123" (volts)
static bool parse_volts(const char* a, double* v) {
  char* end = nullptr;
  double x = strtod(a, &end);
  if (end == a) return false;
  while (*end == ' ') end++;
  if      (!strncasecmp(end, "mv", 2)) x *= 1e-3;
  else if (!strncasecmp(end, "uv", 2)) x *= 1e-6;
  else if (!strncasecmp(end, "nv", 2)) x *= 1e-9;
  *v = x;
  return true;
}

static void print_help(Print& o) {
  o.println(F("--- Nanovoltmeter CLI ---"));
  o.println(F("read              last reading + stats"));
  o.println(F("run | stop        start/stop measuring"));
  o.println(F("range 20mv|200mv|auto|manual"));
  o.println(F("mode chop|dc      lowZ-chop / highZ auto-zero"));
  o.println(F("chop <ms>         chop half-period"));
  o.println(F("settle <ms>       post-relay settle"));
  o.println(F("aper <ms>         highZ aperture"));
  o.println(F("azint <s>         auto-zero interval (0=off)"));
  o.println(F("bl <1-5>          TFT backlight level"));
  o.println(F("az                trigger auto-zero now"));
  o.println(F("stats [reset]"));
  o.println(F("zero              run zero cal (shorts input)"));
  o.println(F("calbias           run bias-current cal (100k std)"));
  o.println(F("calgain <v>       e.g. calgain 8.2mV (apply ref first)"));
  o.println(F("calabort | caldump"));
  o.println(F("state             engine/debug dump"));
  o.println(F("bias <pA>         DEBUG: servo -1000..+500 pA"));
  o.println(F("relay <3|4|5> <s|r> DEBUG: pulse mech relay set/reset"));
  o.println(F("chopssr f|r       DEBUG: SSR chop bridge polarity"));
  o.println(F("touch             DEBUG: last touch point (calibration)"));
  o.println(F("json              status JSON"));
  o.println(F("net | dhcp on|off | ip/mask/gw <a.b.c.d> | hostname <n> | netapply"));
  o.println(F("demo on|off       simulated front end (reboot applies)"));
  o.println(F("save | defaults | reboot"));
}

static void print_state(Print& o, const NvState& s) {
  o.print(F("phase="));    o.print(nv_phase_name(s.phase));
  o.print(F(" running=")); o.print(s.running);
  o.print(F(" range="));   o.print(nv_range_name(s.range));
  o.print(F(" mode="));    o.println(nv_mode_name(s.mode));
  o.print(F("ssr: chop_rev=")); o.print(relays_chop_get());
  o.print(F(" range_g21="));    o.print(relays_range_get());
  o.print(F("  relays: K3_short=")); o.print(relays_get(RLY_K3_SHORT));
  o.print(F(" K4_cal="));       o.print(relays_get(RLY_K4_CAL));
  o.print(F(" K5_bridge="));    o.print(relays_get(RLY_K5_BRIDGE));
  o.print(F(" busy="));         o.println(relays_busy());
  o.print(F("adc_v=")); o.print(s.adc_v, 6);
  o.print(F(" samples=")); o.print(adc_sample_count());
  o.print(F(" overload=")); o.println(s.overload);
  o.print(F("gain=")); o.print(nv_gain(s), 4);
  o.print(F(" live_zero=")); o.print(s.live_zero_v * 1e9, 1); o.println(F(" nV"));
  o.print(F("bias_servo=")); o.print(bias_get_pa(), 1);
  o.println(F(" pA"));
  o.print(F("cal: busy=")); o.print(s.cal_busy);
  o.print(F(" msg=")); o.println(s.cal_msg);
}

static void print_net(Print& o) {
  NvSettings ns;
  nv_settings_load(ns);
  char b[20];
  o.print(F("dhcp="));     o.println(ns.use_dhcp ? F("on") : F("off"));
  nv_fmt_ip(ns.static_ip, b); o.print(F("ip="));   o.println(b);
  nv_fmt_ip(ns.subnet, b);    o.print(F("mask=")); o.println(b);
  nv_fmt_ip(ns.gateway, b);   o.print(F("gw="));   o.println(b);
  o.print(F("hostname=")); o.println(ns.hostname);
}

// ------------------------------------------------------------
void comms_process(const String& line, Print& o, NvState& s, CommsFlags& f) {
  char buf[96];
  line.toCharArray(buf, sizeof(buf));
  char* save = nullptr;
  char* cmd = strtok_r(buf, " \t", &save);
  if (!cmd) return;
  char* a1 = strtok_r(nullptr, " \t", &save);
  char* a2 = strtok_r(nullptr, " \t", &save);

  if (!strcasecmp(cmd, "help") || !strcmp(cmd, "?")) { print_help(o); return; }

  if (!strcasecmp(cmd, "read") || !strcasecmp(cmd, "status")) {
    print_reading(o, s); return;
  }
  if (!strcasecmp(cmd, "run"))  { measure_start(s); o.println(F("running")); return; }
  if (!strcasecmp(cmd, "stop")) { measure_stop(s);  o.println(F("stopped")); return; }

  if (!strcasecmp(cmd, "range") && a1) {
    if      (!strcasecmp(a1, "20mv"))   { s.autorange = false; measure_set_range(s, RANGE_20MV); }
    else if (!strcasecmp(a1, "200mv"))  { s.autorange = false; measure_set_range(s, RANGE_200MV); }
    else if (!strcasecmp(a1, "auto"))   { s.autorange = true; }
    else if (!strcasecmp(a1, "manual")) { s.autorange = false; }
    else { o.println(F("range 20mv|200mv|auto|manual")); return; }
    f.settings_dirty = true;
    o.print(F("range ")); o.print(nv_range_name(s.range));
    o.println(s.autorange ? F(" (auto)") : F(""));
    return;
  }

  if (!strcasecmp(cmd, "mode") && a1) {
    NvMode m = (!strcasecmp(a1, "chop")) ? MODE_LOWZ_CHOP :
               (!strcasecmp(a1, "dc"))   ? MODE_HIGHZ_AZ  : (NvMode)255;
    if (m == (NvMode)255) { o.println(F("mode chop|dc")); return; }
    s.mode = m;
    if (s.running) { measure_stop(s); measure_start(s); }
    f.settings_dirty = true;
    o.print(F("mode ")); o.println(nv_mode_name(m));
    return;
  }

  struct { const char* name; uint16_t* val; int lo, hi; } nums[] = {
    { "chop",   &s.chop_half_ms,    5, 10000 },
    { "settle", &s.settle_ms,      20,  5000 },
    { "aper",   &s.aperture_ms,    50, 60000 },
    { "azint",  &s.az_interval_s,   0,  3600 },
  };
  for (auto& nc : nums) {
    if (!strcasecmp(cmd, nc.name) && a1) {
      int v = atoi(a1);
      if (v < nc.lo || v > nc.hi) { o.println(F("out of range")); return; }
      *nc.val = (uint16_t)v;
      f.settings_dirty = true;
      o.print(nc.name); o.print(F(" = ")); o.println(v);
      return;
    }
  }

  if (!strcasecmp(cmd, "bl") && a1) {
    int v = atoi(a1);
    if (v < BL_LEVEL_MIN || v > BL_LEVEL_MAX) { o.println(F("bl 1..5")); return; }
    s.backlight = (uint8_t)v;
    display_backlight(s.backlight);
    f.settings_dirty = true;
    o.print(F("backlight ")); o.println(v);
    return;
  }

  if (!strcasecmp(cmd, "az")) { measure_trigger_az(s); o.println(F("az queued")); return; }

  if (!strcasecmp(cmd, "stats")) {
    if (a1 && !strcasecmp(a1, "reset")) { s.stats.reset(); o.println(F("stats reset")); }
    else print_reading(o, s);
    return;
  }

  // ---- calibration ------------------------------------------
  if (!strcasecmp(cmd, "zero")) {
    o.println(cal_start_zero(s) ? F("zero cal started") : F("busy")); return;
  }
  if (!strcasecmp(cmd, "calbias")) {
    o.println(cal_start_bias(s) ? F("bias cal started (takes ~1 min)") : F("busy"));
    return;
  }
  if (!strcasecmp(cmd, "calgain") && a1) {
    double v;
    if (!parse_volts(a1, &v)) { o.println(F("calgain <value>[mV|uV]")); return; }
    if (cal_gain_from_expected(s, v)) {
      o.print(F("gain corr ")); o.print(nv_range_name(s.range));
      o.print(F(" -> ")); o.println(s.cal.gain_corr[s.range], 6);
    } else o.println(F("rejected (no stable reading, or correction >10%)"));
    return;
  }
  if (!strcasecmp(cmd, "calabort")) { cal_abort(s); o.println(F("aborted")); return; }
  if (!strcasecmp(cmd, "caldump")) {
    o.print(F("gain_corr: ")); o.print(s.cal.gain_corr[0], 6);
    o.print(' ');              o.println(s.cal.gain_corr[1], 6);
    for (int m = 0; m < 2; m++)
      for (int r = 0; r < 2; r++) {
        o.print(F("zero[")); o.print(m); o.print(']'); o.print('[');
        o.print(r); o.print(F("] = "));
        o.print(s.cal.zero_v[m][r] * 1e9, 1); o.println(F(" nV"));
      }
    o.print(F("bias_pa=")); o.print(s.cal.bias_pa, 1);
    o.print(F(" ib_pa=")); o.println(s.cal.ib_pa, 1);
    return;
  }

  // ---- debug ------------------------------------------------
  if (!strcasecmp(cmd, "state")) { print_state(o, s); return; }
  if (!strcasecmp(cmd, "bias") && a1) {
    bias_set_pa((float)atof(a1));
    o.print(F("bias servo = ")); o.print(bias_get_pa(), 1);
    o.println(F(" pA"));
    return;
  }
  if (!strcasecmp(cmd, "relay") && a1 && a2) {
    int k = atoi(a1) - 3;              // K3/K4/K5
    if (k < 0 || k > 2) { o.println(F("relay 3|4|5 s|r")); return; }
    relays_request((RelayId)k, tolower(a2[0]) == 's', true);
    o.println(F("pulsed"));
    return;
  }
  if (!strcasecmp(cmd, "chopssr") && a1) {
    relays_chop(tolower(a1[0]) == 'r');
    o.print(F("chop bridge: "));
    o.println(relays_chop_get() ? F("REVERSED") : F("forward"));
    return;
  }

  if (!strcasecmp(cmd, "touch")) { display_touch_debug(o); return; }

  if (!strcasecmp(cmd, "json")) {
    comms_json_status(o, s, "", millis());
    o.println();
    return;
  }

  // ---- network / persistence --------------------------------
  if (!strcasecmp(cmd, "net")) { print_net(o); return; }
  if (!strcasecmp(cmd, "dhcp") && a1) {
    NvSettings ns; nv_settings_load(ns);
    ns.use_dhcp = !strcasecmp(a1, "on");
    nv_settings_save(ns);
    o.println(F("saved — 'netapply' or reboot to apply"));
    return;
  }
  if ((!strcasecmp(cmd, "ip") || !strcasecmp(cmd, "mask") ||
       !strcasecmp(cmd, "gw")) && a1) {
    NvSettings ns; nv_settings_load(ns);
    uint8_t* dst = !strcasecmp(cmd, "ip")   ? ns.static_ip :
                   !strcasecmp(cmd, "mask") ? ns.subnet    : ns.gateway;
    if (!nv_parse_ip(a1, dst)) { o.println(F("bad address")); return; }
    nv_settings_save(ns);
    o.println(F("saved — 'netapply' or reboot to apply"));
    return;
  }
  if (!strcasecmp(cmd, "hostname") && a1) {
    NvSettings ns; nv_settings_load(ns);
    strncpy(ns.hostname, a1, sizeof(ns.hostname) - 1);
    ns.hostname[sizeof(ns.hostname) - 1] = '\0';
    nv_settings_save(ns);
    o.println(F("saved — 'netapply' or reboot to apply"));
    return;
  }
  if (!strcasecmp(cmd, "netapply")) { f.net_reinit = true; o.println(F("applying...")); return; }

  if (!strcasecmp(cmd, "demo") && a1) {
    s.demo_mode = !strcasecmp(a1, "on");
    f.settings_dirty = true;
    o.println(F("saved — reboot to apply"));
    return;
  }
  if (!strcasecmp(cmd, "save"))     { f.settings_dirty = true; o.println(F("saving")); return; }
  if (!strcasecmp(cmd, "defaults")) {
    cal_defaults(s.cal); cal_save(s.cal);
    NvSettings ns; nv_settings_load(ns); ns.magic = 0; nv_settings_save(ns);
    o.println(F("EEPROM invalidated — reboot for defaults"));
    return;
  }
  if (!strcasecmp(cmd, "reboot")) { f.reboot = true; o.println(F("rebooting...")); return; }

  o.println(F("unknown command — 'help'"));
}

// ------------------------------------------------------------
void comms_json_status(Print& o, const NvState& s, const char* ip,
                       uint32_t up_ms) {
  o.print(F("{\"reading_v\":"));
  if (isnan(s.reading_v)) o.print(F("null")); else o.print(s.reading_v, 10);
  o.print(F(",\"sigma_v\":"));
  if (s.stats.n > 1) o.print(s.stats.sigma(), 10); else o.print(F("null"));
  o.print(F(",\"mean_v\":"));
  if (s.stats.n) o.print(s.stats.mean, 10); else o.print(F("null"));
  o.print(F(",\"n\":"));         o.print(s.stats.n);
  o.print(F(",\"running\":"));   o.print(s.running ? F("true") : F("false"));
  o.print(F(",\"overload\":"));  o.print(s.overload ? F("true") : F("false"));
  o.print(F(",\"range\":\""));   o.print(nv_range_name(s.range));
  o.print(F("\",\"autorange\":")); o.print(s.autorange ? F("true") : F("false"));
  o.print(F(",\"mode\":\""));    o.print(nv_mode_name(s.mode));
  o.print(F("\",\"phase\":\"")); o.print(nv_phase_name(s.phase));
  o.print(F("\",\"chop_half_ms\":")); o.print(s.chop_half_ms);
  o.print(F(",\"settle_ms\":"));      o.print(s.settle_ms);
  o.print(F(",\"aperture_ms\":"));    o.print(s.aperture_ms);
  o.print(F(",\"az_interval_s\":"));  o.print(s.az_interval_s);
  o.print(F(",\"backlight\":"));      o.print(s.backlight);
  o.print(F(",\"az_age_s\":"));       o.print(s.az_age_s);
  o.print(F(",\"adc_v\":"));          o.print(s.adc_v, 6);
  o.print(F(",\"gain\":"));           o.print(nv_gain(s), 5);
  o.print(F(",\"gain_corr\":["));     o.print(s.cal.gain_corr[0], 6);
  o.print(',');                       o.print(s.cal.gain_corr[1], 6);
  o.print(F("],\"zero_nv\":"));       o.print(s.live_zero_v * 1e9, 1);
  o.print(F(",\"bias_pa\":"));        o.print(s.cal.bias_pa, 1);
  o.print(F(",\"ib_pa\":"));          o.print(s.cal.ib_pa, 1);
  o.print(F(",\"cal_busy\":"));       o.print(s.cal_busy ? F("true") : F("false"));
  o.print(F(",\"cal_msg\":\""));      o.print(s.cal_msg);
  o.print(F("\",\"demo\":"));         o.print(s.demo_mode ? F("true") : F("false"));
  o.print(F(",\"ip\":\""));           o.print(ip);
  o.print(F("\",\"uptime_s\":"));     o.print(up_ms / 1000UL);
  o.print(F("}"));
}
