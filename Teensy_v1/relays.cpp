#include <Arduino.h>
#include "config.h"
#include "relays.h"

// coil pin pairs [relay][0]=SET, [1]=RESET
static const uint8_t COIL[2][2] = {
  { PIN_K3_SET, PIN_K3_RST },
  { PIN_K4_SET, PIN_K4_RST },
};
#define N_COIL  ((int)(sizeof(COIL) / sizeof(COIL[0])))

static bool     s_state[N_COIL] = { false, false };
static bool     s_chop_rev = false;
static bool     s_range_g21 = false;
static bool     s_direct = false;
static uint8_t  s_queue[8];          // pending pulses: relay*2 + set
static uint8_t  s_qhead = 0, s_qtail = 0;
static int8_t   s_active_pin = -1;
static uint32_t s_phase_t0 = 0;
static bool     s_in_gap = false;

static inline bool q_empty() { return s_qhead == s_qtail; }
static inline void q_push(uint8_t v) {
  uint8_t nt = (s_qtail + 1) & 7;
  if (nt != s_qhead) { s_queue[s_qtail] = v; s_qtail = nt; }
}

void relays_init() {
  for (int k = 0; k < N_COIL; k++)
    for (int c = 0; c < 2; c++) {
      pinMode(COIL[k][c], OUTPUT);
      digitalWrite(COIL[k][c], LOW);
    }
  pinMode(PIN_CHOP, OUTPUT);
  pinMode(PIN_RANGE, OUTPUT);
  pinMode(PIN_DIRECT, OUTPUT);
  pinMode(PIN_FAULT, INPUT_PULLUP);
  digitalWrite(PIN_CHOP, LOW);       // forward
  digitalWrite(PIN_RANGE, LOW);      // G=201 / 20 mV
  s_chop_rev = false;
  s_range_g21 = false;
  relays_direct(true);               // boot idle = source-direct high-Z path
  // Force every mechanical relay to its RESET (safe) state at boot:
  // K3 open, K4 open.
  for (int k = 0; k < N_COIL; k++) {
    s_state[k] = true;               // pretend SET so request(false) queues
    relays_request((RelayId)k, false, true);
  }
}

void relays_request(RelayId k, bool set, bool force) {
  if (!force && s_state[k] == set) return;
  s_state[k] = set;
  q_push((uint8_t)(k * 2 + (set ? 1 : 0)));
}

bool relays_get(RelayId k) { return s_state[k]; }
bool relays_busy() { return s_active_pin >= 0 || !q_empty(); }

void relays_tick(uint32_t now) {
  if (s_active_pin >= 0) {
    if (!s_in_gap && now - s_phase_t0 >= RELAY_PULSE_MS) {
      digitalWrite((uint8_t)s_active_pin, LOW);
      s_in_gap = true;
      s_phase_t0 = now;
    } else if (s_in_gap && now - s_phase_t0 >= RELAY_GAP_MS) {
      s_active_pin = -1;
    }
    return;
  }
  if (q_empty()) return;
  uint8_t v = s_queue[s_qhead];
  s_qhead = (s_qhead + 1) & 7;
  uint8_t pin = COIL[v >> 1][(v & 1) ? 0 : 1];
  digitalWrite(pin, HIGH);
  s_active_pin = pin;
  s_in_gap = false;
  s_phase_t0 = now;
}

// ---- SSR levels ---------------------------------------------
void relays_chop(bool rev)   { s_chop_rev = rev;  digitalWrite(PIN_CHOP, rev); }
bool relays_chop_get()       { return s_chop_rev; }
void relays_range(bool g21)  { s_range_g21 = g21; digitalWrite(PIN_RANGE, g21); }
bool relays_range_get()      { return s_range_g21; }
void relays_direct(bool d)   { s_direct = d;      digitalWrite(PIN_DIRECT, d); }
bool relays_direct_get()     { return s_direct; }

// FAULT_N is active low: LOW = SEC7 protection tripped.
bool relays_fault()          { return digitalRead(PIN_FAULT) == LOW; }
