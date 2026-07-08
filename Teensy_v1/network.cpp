#include <Arduino.h>
#include <QNEthernet.h>
#include "network.h"
#include "nv_settings.h"
#include "webpage.h"

using namespace qindesign::network;

static EthernetServer http_server(80);
static EthernetServer telnet_server(23);
static EthernetClient telnet_client;

static char ip_buf[20];
static char tln_buf[128];
static int  tln_len = 0;

static bool read_line(EthernetClient& c, char* buf, int maxlen, uint32_t timeout_ms) {
  uint32_t t0 = millis();
  int i = 0;
  while (millis() - t0 < timeout_ms) {
    if (c.available()) {
      char ch = (char)c.read();
      if (ch == '\n') {
        if (i > 0 && buf[i - 1] == '\r') i--;
        buf[i] = '\0';
        return true;
      }
      if (i < maxlen - 1) buf[i++] = ch;
    }
  }
  return false;
}

static int read_bytes(EthernetClient& c, char* buf, int n, uint32_t timeout_ms) {
  uint32_t t0 = millis();
  int got = 0;
  while (got < n && millis() - t0 < timeout_ms) {
    if (c.available()) buf[got++] = (char)c.read();
  }
  buf[got] = '\0';
  return got;
}

static void send_headers(EthernetClient& c, const char* status,
                         const char* content_type, bool cors = false) {
  c.print(F("HTTP/1.0 ")); c.println(status);
  c.print(F("Content-Type: ")); c.println(content_type);
  if (cors) c.println(F("Access-Control-Allow-Origin: *"));
  c.println();
}

static void handle_http(EthernetClient& c, NvState& s, CommsFlags& f) {
  char line[128];
  if (!read_line(c, line, sizeof(line), 500)) { c.stop(); return; }

  char method[8] = {}, path[64] = {};
  sscanf(line, "%7s %63s", method, path);

  int content_length = 0;
  while (read_line(c, line, sizeof(line), 200)) {
    if (line[0] == '\0') break;
    if (strncasecmp(line, "Content-Length:", 15) == 0)
      content_length = atoi(line + 15);
  }

  if (strcmp(method, "OPTIONS") == 0) {
    c.println(F("HTTP/1.0 204 No Content"));
    c.println(F("Access-Control-Allow-Origin: *"));
    c.println(F("Access-Control-Allow-Methods: GET, POST, OPTIONS"));
    c.println(F("Access-Control-Allow-Headers: Content-Type"));
    c.println();
    c.stop();
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
    send_headers(c, "200 OK", "text/html; charset=utf-8");
    c.print(WEBPAGE_HTML);
    c.stop();
    return;
  }

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/status") == 0) {
    send_headers(c, "200 OK", "application/json", true);
    comms_json_status(c, s, ip_buf, millis());
    c.stop();
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/cmd") == 0) {
    char body[128] = {};
    if (content_length > 0)
      read_bytes(c, body, min(content_length, (int)sizeof(body) - 1), 300);
    String cmd(body);
    cmd.trim();
    send_headers(c, "200 OK", "text/plain; charset=utf-8", true);
    comms_process(cmd, c, s, f);
    c.stop();
    return;
  }

  send_headers(c, "404 Not Found", "text/plain");
  c.println(F("Not found"));
  c.stop();
}

static void handle_telnet(NvState& s, CommsFlags& f) {
  if (!telnet_client || !telnet_client.connected()) {
    EthernetClient nc = telnet_server.accept();
    if (nc) {
      telnet_client = nc;
      tln_len = 0;
      telnet_client.println(F("Nanovoltmeter CLI v1.0"));
      telnet_client.println(F("Type 'help' for commands."));
      telnet_client.print(F("> "));
    }
    return;
  }
  while (telnet_client.available()) {
    char c = (char)telnet_client.read();
    if (c == '\n' || c == '\r') {
      if (tln_len > 0) {
        tln_buf[tln_len] = '\0';
        String ln(tln_buf);
        comms_process(ln, telnet_client, s, f);
        telnet_client.print(F("> "));
        tln_len = 0;
      }
    } else if (c == 127 || c == '\b') {
      if (tln_len > 0) tln_len--;
    } else if (c >= 0x20 && tln_len < 127) {
      tln_buf[tln_len++] = c;
    }
  }
}

static void do_init() {
  NvSettings s;
  nv_settings_load(s);

  Ethernet.setHostname(s.hostname);

  bool ok = false;
  if (s.use_dhcp) {
    Serial.print(F("Ethernet: DHCP..."));
    ok = Ethernet.begin();
    Ethernet.waitForLocalIP(3000);
    if (!ok || Ethernet.localIP() == INADDR_NONE) {
      Serial.print(F(" timed out, falling back to static "));
      ok = false;
    }
  }
  if (!ok) {
    if (!s.use_dhcp) Serial.print(F("Ethernet: static "));
    IPAddress sip(s.static_ip[0], s.static_ip[1], s.static_ip[2], s.static_ip[3]);
    IPAddress sub(s.subnet[0], s.subnet[1], s.subnet[2], s.subnet[3]);
    IPAddress gw (s.gateway[0], s.gateway[1], s.gateway[2], s.gateway[3]);
    Ethernet.begin(sip, sub, gw);
    Ethernet.waitForLocalIP(1000);
  }

  IPAddress ip = Ethernet.localIP();
  snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  Serial.print(F(" IP: "));        Serial.println(ip_buf);
  Serial.print(F("  hostname: ")); Serial.print(s.hostname); Serial.println(F(".local"));
  Serial.println(F("  HTTP port 80 | Telnet port 23"));
}

void network_init() {
  do_init();
  http_server.begin();
  telnet_server.begin();
}

void network_reinit() {
  Serial.println(F("network_reinit: restarting Ethernet..."));
  if (telnet_client) { telnet_client.stop(); tln_len = 0; }
  http_server.end();
  telnet_server.end();
  delay(50);
  do_init();
  http_server.begin();
  telnet_server.begin();
}

void network_poll(NvState& s, CommsFlags& f) {
  Ethernet.loop();
  for (int i = 0; i < 4; i++) {
    EthernetClient client = http_server.accept();
    if (!client) break;
    handle_http(client, s, f);
  }
  handle_telnet(s, f);
}

const char* network_ip() { return ip_buf; }
