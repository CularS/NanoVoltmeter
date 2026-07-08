#pragma once
#include <Arduino.h>
#include "nv_state.h"

// One command processor shared by USB serial, telnet and the web
// console (POST /api/cmd) — identical behaviour on all three.

struct CommsFlags {
  bool settings_dirty = false;   // persist NvSettings soon
  bool net_reinit     = false;   // re-init Ethernet after net changes
  bool reboot         = false;
};

void comms_process(const String& line, Print& out, NvState& s, CommsFlags& f);
void comms_json_status(Print& out, const NvState& s, const char* ip,
                       uint32_t uptime_ms);
