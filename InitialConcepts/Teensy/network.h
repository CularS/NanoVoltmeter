#pragma once
#include "nv_state.h"
#include "comms.h"

// Ethernet services (QNEthernet, Teensy 4.1 native MAC):
//   HTTP  port 80 — dashboard page, /api/status JSON, /api/cmd
//   Telnet port 23 — same CLI as USB serial
void network_init();
void network_reinit();
void network_poll(NvState& s, CommsFlags& f);
const char* network_ip();
