#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <cstring>
#include "WebServerControl.h"
#include "../wifi/WiFiControl.h"
#include "../wifi/WiFiDiagnostics.h"
#include "../network/NetConfig.h"
#include "../relay/Relay.h"

namespace WebControl {
namespace {
WebServer server(80);
bool serverStarted = false;

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += input[i]; break;
    }
  }
  return out;
}

String statusText() {
  if (!WiFiControl::isEnabled()) return "OFF";
  if (WiFi.status() == WL_CONNECTED) return "CONNECTED";
  return "DISCONNECTED";
}

String tabName() {
  String tab = server.hasArg("tab") ? server.arg("tab") : "dashboard";
  if (tab != "dashboard" && tab != "wifi" && tab != "network" &&
      tab != "diagnostics" && tab != "storage" && tab != "system") {
    tab = "dashboard";
  }
  return tab;
}

void redirect(const char* tab) {
  String location = "/?tab=";
  location += tab;
  server.sendHeader("Location", location, true);
  server.send(303, "text/plain", "Redirecting");
}

String txPowerText() {
  int8_t txPower = 0;
  if (esp_wifi_get_max_tx_power(&txPower) != ESP_OK) return "unavailable";
  String value = String(static_cast<float>(txPower) * 0.25f, 2);
  value += F(" dBm");
  return value;
}

String firmwareBuild() {
#ifdef FW_BUILD_VERSION
  return String(FW_BUILD_VERSION);
#else
  return "0";
#endif
}

String nvsTypeName(nvs_type_t type) {
  switch (type) {
    case NVS_TYPE_U8: return "U8";
    case NVS_TYPE_I8: return "I8";
    case NVS_TYPE_U16: return "U16";
    case NVS_TYPE_I16: return "I16";
    case NVS_TYPE_U32: return "U32";
    case NVS_TYPE_I32: return "I32";
    case NVS_TYPE_U64: return "U64";
    case NVS_TYPE_I64: return "I64";
    case NVS_TYPE_STR: return "STRING";
    case NVS_TYPE_BLOB: return "BLOB";
    default: return "UNKNOWN";
  }
}

bool sensitiveNvsKey(const char* key) {
  return strcasecmp(key, "password") == 0 ||
         strcasecmp(key, "passphrase") == 0 ||
         strcasecmp(key, "token") == 0;
}

String nvsValue(const nvs_entry_info_t& entry) {
  if (sensitiveNvsKey(entry.key)) return "<hidden>";

  nvs_handle_t handle = 0;
  if (nvs_open_from_partition("nvs", entry.namespace_name,
                              NVS_READONLY, &handle) != ESP_OK) {
    return "<read error>";
  }

  String value = "<unsupported>";
  switch (entry.type) {
    case NVS_TYPE_U8: {
      uint8_t v = 0; if (nvs_get_u8(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I8: {
      int8_t v = 0; if (nvs_get_i8(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U16: {
      uint16_t v = 0; if (nvs_get_u16(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I16: {
      int16_t v = 0; if (nvs_get_i16(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U32: {
      uint32_t v = 0; if (nvs_get_u32(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I32: {
      int32_t v = 0; if (nvs_get_i32(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U64: {
      uint64_t v = 0; if (nvs_get_u64(handle, entry.key, &v) == ESP_OK) value = String((unsigned long long)v); break;
    }
    case NVS_TYPE_I64: {
      int64_t v = 0; if (nvs_get_i64(handle, entry.key, &v) == ESP_OK) value = String((long long)v); break;
    }
    case NVS_TYPE_STR: {
      size_t len = 0;
      if (nvs_get_str(handle, entry.key, nullptr, &len) == ESP_OK && len > 0) {
        char* buffer = new char[len];
        if (buffer != nullptr && nvs_get_str(handle, entry.key, buffer, &len) == ESP_OK) value = buffer;
        delete[] buffer;
      } else {
        value = "<empty>";
      }
      break;
    }
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if (nvs_get_blob(handle, entry.key, nullptr, &len) == ESP_OK) {
        value = "<";
        value += String(static_cast<unsigned>(len));
        value += F(" bytes>");
      }
      break;
    }
    default:
      break;
  }
  nvs_close(handle);
  return value;
}

String nvsTable() {
  String html;
  html.reserve(5000);
  html += F("<div class='table-wrap'><table><thead><tr><th>#</th><th>Namespace</th><th>Key</th><th>Type</th><th>Value</th></tr></thead><tbody>");

  nvs_iterator_t iterator = nullptr;
  size_t count = 0;
  esp_err_t result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &iterator);
  while (result == ESP_OK && iterator != nullptr) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);
    ++count;

    html += F("<tr><td>");
    html += String(count);
    html += F("</td><td>");
    html += htmlEscape(info.namespace_name);
    html += F("</td><td>");
    html += htmlEscape(info.key);
    html += F("</td><td>");
    html += nvsTypeName(info.type);
    html += F("</td><td>");
    html += htmlEscape(nvsValue(info));
    html += F("</td></tr>");

    result = nvs_entry_next(&iterator);
  }

  if (iterator != nullptr) nvs_release_iterator(iterator);

  html += F("</tbody></table></div><div class='muted'>");
  html += String(count);
  html += F(" entries. Password/token values are hidden.</div>");
  return html;
}

String page() {
  const String tab = tabName();
  const bool enabled = WiFiControl::isEnabled();
  const bool connected = enabled && WiFi.status() == WL_CONNECTED;
  const uint32_t uptime = millis() / 1000UL;

  String html;
  html.reserve(18000);

  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32-C3 PC Relay</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:1100px;margin:auto}");
  html += F("h1{font-size:25px;margin:0 0 4px}h2{font-size:18px;margin:0 0 14px}h3{font-size:15px;margin:20px 0 8px}.muted{color:#999}.ok{color:#62d477}.warn{color:#e5bd58}.bad{color:#ff7070}");
  html += F(".tabs{display:flex;gap:6px;overflow:auto;margin:18px 0 12px;padding-bottom:2px}.tabs a{color:#bbb;text-decoration:none;padding:10px 13px;border:1px solid #383838;border-radius:8px;white-space:nowrap;background:#191919}.tabs a.active{color:#fff;background:#303030;border-color:#666}");
  html += F(".card{background:#1c1c1c;border:1px solid #414141;border-radius:10px;padding:16px;margin:12px 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}");
  html += F("table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px 7px;border-bottom:1px solid #333;vertical-align:middle}th{color:#bbb;font-weight:600}td:first-child{color:#aaa}");
  html += F(".table-wrap{overflow:auto}.kv td:first-child{width:38%}");
  html += F("label{display:block;color:#bbb;font-size:13px;margin:0 0 5px}input,select{width:100%;font:inherit;color:#eee;background:#111;border:1px solid #555;border-radius:7px;padding:10px}input:focus,select:focus{outline:2px solid #507eb7;border-color:#507eb7}");
  html += F("input[type=number]{appearance:textfield}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:10px 0}.help{font-size:12px;color:#888;margin-top:4px}");
  html += F("button{font:inherit;padding:10px 15px;border:0;border-radius:7px;margin:5px 6px 0 0;color:white;background:#315f93;cursor:pointer}.danger{background:#7b3030}.good{background:#286a39}.secondary{background:#454545}");
  html += F(".status{display:inline-block;padding:5px 9px;border-radius:20px;background:#292929}.mono{font-family:ui-monospace,SFMono-Regular,monospace}.notice{padding:11px 13px;border:1px solid #555;border-radius:8px;background:#252525;margin:12px 0}");
  html += F("</style></head><body>");

  html += F("<h1>ESP32-C3 PC Relay</h1><div class='muted'>Headless control and configuration</div>");

  html += F("<nav class='tabs'>");
  const char* names[] = {"dashboard","wifi","network","diagnostics","storage","system"};
  const char* labels[] = {"Dashboard","Wi-Fi","Network","Diagnostics","Storage","System"};
  for (size_t i = 0; i < 6; ++i) {
    html += F("<a href='/?tab=");
    html += names[i];
    html += F("' class='");
    if (tab == names[i]) html += F("active");
    html += F("'>");
    html += labels[i];
    html += F("</a>");
  }
  html += F("</nav>");

  if (tab == "dashboard") {
    html += F("<div class='grid'><div class='card'><h2>Wi-Fi</h2><table class='kv'>");
    html += F("<tr><td>Status</td><td class='");
    html += connected ? F("ok'>CONNECTED") : (enabled ? F("warn'>DISCONNECTED") : F("muted'>OFF"));
    html += F("</td></tr><tr><td>SSID</td><td>");
    html += connected ? htmlEscape(WiFi.SSID()) : F("-");
    html += F("</td></tr><tr><td>IP address</td><td class='mono'>");
    html += connected ? WiFi.localIP().toString() : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr><tr><td>Channel</td><td>");
    html += connected ? String(WiFi.channel()) : F("-");
    html += F("</td></tr></table></div>");

    html += F("<div class='card'><h2>System</h2><table class='kv'><tr><td>Uptime</td><td>");
    html += String(uptime);
    html += F(" seconds</td></tr><tr><td>Firmware build</td><td>");
    html += firmwareBuild();
    html += F("</td></tr><tr><td>ESP-IDF</td><td>");
    html += esp_get_idf_version();
    html += F("</td></tr><tr><td>CPU</td><td>");
    html += String(getCpuFrequencyMhz());
    html += F(" MHz</td></tr></table></div></div>");

    html += F("<div class='card'><h2>Relay</h2><table class='kv'><tr><td>POWER relay</td><td>");
    html += Relay::powerOn() ? F("<span class='ok'>ON</span>") : F("OFF");
    html += F(" &nbsp; GPIO5</td></tr><tr><td>RESET relay</td><td>");
    html += Relay::resetOn() ? F("<span class='ok'>ON</span>") : F("OFF");
    html += F(" &nbsp; GPIO6</td></tr></table></div>");
  }

  if (tab == "wifi") {
    html += F("<div class='card'><h2>Wi-Fi</h2><div class='status'>");
    html += statusText();
    html += F("</div><table class='kv'><tr><td>SSID</td><td>");
    html += connected ? htmlEscape(WiFi.SSID()) : F("-");
    html += F("</td></tr><tr><td>IP address</td><td class='mono'>");
    html += connected ? WiFi.localIP().toString() : F("-");
    html += F("</td></tr><tr><td>Gateway</td><td class='mono'>");
    html += connected ? WiFi.gatewayIP().toString() : F("-");
    html += F("</td></tr><tr><td>Subnet</td><td class='mono'>");
    html += connected ? WiFi.subnetMask().toString() : F("-");
    html += F("</td></tr><tr><td>DNS</td><td class='mono'>");
    html += connected ? WiFi.dnsIP().toString() : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr><tr><td>Channel</td><td>");
    html += connected ? String(WiFi.channel()) : F("-");
    html += F("</td></tr><tr><td>BSSID</td><td class='mono'>");
    html += connected ? htmlEscape(WiFi.BSSIDstr()) : F("-");
    html += F("</td></tr><tr><td>TX power</td><td>");
    html += txPowerText();
    html += F("</td></tr></table>");
    html += F("<form method='POST' action='/wifi/toggle'><button class='");
    html += enabled ? F("danger'>Turn Wi-Fi OFF") : F("good'>Turn Wi-Fi ON");
    html += F("</button></form>");
    if (enabled) html += F("<form method='POST' action='/wifi/reconnect'><button>Reconnect now</button></form>");
    html += F("</div>");

    html += F("<div class='card'><h2>Wi-Fi credentials</h2><div class='muted'>Credentials are saved only after a successful connection. SSID is limited to 32 characters and password to 63.</div>");
    html += F("<form method='POST' action='/wifi/save'><div class='row'><div><label for='ssid'>SSID</label><input id='ssid' name='ssid' maxlength='32' required value='");
    html += htmlEscape(WiFiControl::savedSSID());
    html += F("'></div><div><label for='password'>Password</label><input id='password' name='password' type='password' maxlength='63' autocomplete='new-password'><div class='help'>Leave empty only for an open network.</div></div></div><button class='good'>Test and save credentials</button></form></div>");

    html += F("<div class='card'><h2>Nearby networks</h2><form method='POST' action='/wifi/scan'><button class='secondary'>Scan now</button></form>");
    html += F("<div class='help'>A scan temporarily pauses normal Wi-Fi connection activity while results are collected.</div></div>");

    html += F("<div class='card'><h2>TX power</h2><form method='POST' action='/wifi/txpower'><div class='row'><div><label for='txpower'>Power (dBm)</label><input id='txpower' name='dbm' type='number' min='2' max='18' step='0.25' value='");
    int8_t txPower = 60;
    if (esp_wifi_get_max_tx_power(&txPower) != ESP_OK) txPower = 60;
    html += String(static_cast<float>(txPower) * 0.25f, 2);
    html += F("'><div class='help'>Allowed: 2.00–18.00 dBm in 0.25 dBm increments.</div></div></div><button>Save TX power</button></form></div>");
  }

  if (tab == "network") {
    const bool isStatic = NetConfig::mode() == NetConfig::Mode::STATIC;
    html += F("<div class='card'><h2>Network configuration</h2><form method='POST' action='/network/save'>");
    html += F("<div class='row'><div><label for='mode'>Address mode</label><select id='mode' name='mode'><option value='dhcp'");
    if (!isStatic) html += F(" selected");
    html += F(">DHCP</option><option value='static'");
    if (isStatic) html += F(" selected");
    html += F(">Manual / Static IPv4</option></select></div></div>");
    html += F("<div class='row'><div><label>IP address</label><input name='ip' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(NetConfig::savedIP());
    html += F("'><div class='help'>IPv4, four octets, maximum 255 each.</div></div><div><label>Gateway</label><input name='gateway' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(NetConfig::savedGateway());
    html += F("'></div><div><label>Subnet mask</label><input name='subnet' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(NetConfig::savedSubnet());
    html += F("'></div></div>");
    html += F("<div class='row'><div><label>DNS 1</label><input name='dns1' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(NetConfig::savedDNS1());
    html += F("'></div><div><label>DNS 2</label><input name='dns2' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(NetConfig::savedDNS2());
    html += F("'></div></div><button class='good'>Save network settings</button></form></div>");
    html += F("<div class='card'><h2>Current effective network</h2><table class='kv'><tr><td>Mode</td><td>");
    html += isStatic ? F("MANUAL / STATIC") : F("DHCP");
    html += F("</td></tr><tr><td>Hostname</td><td class='mono'>esp32-c3-relay</td></tr><tr><td>IP</td><td class='mono'>");
    html += NetConfig::currentIP();
    html += F("</td></tr><tr><td>Gateway</td><td class='mono'>");
    html += NetConfig::currentGateway();
    html += F("</td></tr></table></div>");
  }

  if (tab == "diagnostics") {
    html += F("<div class='card'><h2>Wi-Fi diagnostics</h2><table class='kv'><tr><td>Diagnostics</td><td class='");
    html += WiFiDiagnostics::enabled() ? F("ok'>ON") : F("bad'>OFF");
    html += F("</td></tr></table><form method='POST' action='/diagnostics/toggle'><button>");
    html += WiFiDiagnostics::enabled() ? F("Turn diagnostics OFF") : F("Turn diagnostics ON");
    html += F("</button></form></div>");
    html += F("<div class='card'><h2>Live connection data</h2><table class='kv'><tr><td>Wi-Fi state</td><td>");
    html += statusText();
    html += F("</td></tr><tr><td>Authentication</td><td>");
    if (connected) {
      wifi_ap_record_t record = {};
      if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) html += WiFiControl::authModeName(record.authmode);
      else html += F("unavailable");
    } else html += F("-");
    html += F("</td></tr><tr><td>BSSID</td><td class='mono'>");
    html += connected ? htmlEscape(WiFi.BSSIDstr()) : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr></table><div class='help'>Detailed connection event records remain available on the serial diagnostics console.</div></div>");
  }

  if (tab == "storage") {
    html += F("<div class='card'><h2>NVS contents</h2>");
    html += nvsTable();
    html += F("</div><div class='card'><h2>Format NVS</h2><div class='bad'>This erases the entire NVS partition, including Wi-Fi credentials, TX power, diagnostics, and network settings.</div>");
    html += F("<form method='POST' action='/nvs/format' onsubmit=\"return confirm('Erase the entire NVS partition and reboot the ESP32?');\"><button class='danger'>Format NVS and reboot</button></form></div>");
  }

  if (tab == "system") {
    html += F("<div class='card'><h2>Firmware</h2><table class='kv'><tr><td>Build</td><td>");
    html += firmwareBuild();
    html += F("</td></tr><tr><td>Build date</td><td>");
    html += __DATE__;
    html += F(" ");
    html += __TIME__;
    html += F("</td></tr><tr><td>ESP-IDF</td><td>");
    html += esp_get_idf_version();
    html += F("</td></tr><tr><td>Arduino core</td><td>");
    html += ESP_ARDUINO_VERSION_STR;
    html += F("</td></tr><tr><td>Chip</td><td>ESP32-C3</td></tr><tr><td>CPU frequency</td><td>");
    html += String(getCpuFrequencyMhz());
    html += F(" MHz</td></tr><tr><td>Uptime</td><td>");
    html += String(uptime);
    html += F(" seconds</td></tr></table></div>");
    html += F("<div class='card'><h2>System actions</h2><form method='POST' action='/system/reboot' onsubmit=\"return confirm('Reboot the ESP32-C3?');\"><button class='danger'>Reboot ESP32-C3</button></form></div>");
  }

  html += F("<div class='muted'>Page status: ");
  html += statusText();
  html += F("</div></body></html>");
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", page());
}

void handleToggle() {
  const bool enable = !WiFiControl::isEnabled();
  if (!enable) {
    // Return the HTTP response before shutting down the interface so the
    // browser receives confirmation even though Wi-Fi will immediately vanish.
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Wi-Fi disabled</h2><p>The ESP32-C3 Wi-Fi interface is now OFF. Re-enable it from the serial console or a future local management interface.</p></body>");
    delay(100);
    WiFiControl::setEnabled(false);
    return;
  }
  WiFiControl::setEnabled(true);
  redirect("wifi");
}

void handleReconnect() {
  WiFiControl::connect();
  redirect("wifi");
}

void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    redirect("wifi");
    return;
  }
  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  WiFiControl::configureCredentials(ssid, password);
  redirect("wifi");
}

void handleWifiScan() {
  if (!WiFiControl::isEnabled()) {
    redirect("wifi");
    return;
  }

  WiFiControl::service();
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.disconnect(false, false);
  delay(100);
  const int count = WiFi.scanNetworks();

  String html;
  html.reserve(9000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Wi-Fi scan</title><style>");
  html += F("body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:1100px;margin:auto}.card{background:#1c1c1c;border:1px solid #414141;border-radius:10px;padding:16px;margin:12px 0}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px 7px;border-bottom:1px solid #333}.muted{color:#999}a,button{display:inline-block;font:inherit;padding:10px 14px;border:0;border-radius:7px;background:#315f93;color:#fff;text-decoration:none;margin-top:12px}</style></head><body>");
  html += F("<div class='card'><h2>Nearby Wi-Fi networks</h2>");

  if (count <= 0) {
    html += F("<div class='muted'>No networks found or scan failed.</div>");
  } else {
    html += F("<div style='overflow:auto'><table><thead><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Channel</th><th>Security</th><th>BSSID</th></tr></thead><tbody>");
    for (int i = 0; i < count; ++i) {
      html += F("<tr><td>");
      html += String(i + 1);
      html += F("</td><td>");
      html += WiFi.SSID(i).isEmpty() ? F("<span class='muted'>(hidden)</span>") : htmlEscape(WiFi.SSID(i));
      html += F("</td><td>");
      html += String(WiFi.RSSI(i));
      html += F(" dBm</td><td>");
      html += String(WiFi.channel(i));
      html += F("</td><td>");
      html += WiFiControl::authModeName(WiFi.encryptionType(i));
      html += F("</td><td class='mono'>");
      html += htmlEscape(WiFi.BSSIDstr(i));
      html += F("</td></tr>");
    }
    html += F("</tbody></table></div><div class='muted'>");
    html += String(count);
    html += F(" access points found.</div>");
  }

  html += F("<a href='/?tab=wifi'>Back to Wi-Fi</a></div></body></html>");
  WiFi.scanDelete();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleTxPower() {
  if (server.hasArg("dbm")) {
    const float value = server.arg("dbm").toFloat();
    WiFiControl::setTxPowerDbm(value);
  }
  redirect("wifi");
}

void handleDiagnosticsToggle() {
  const bool desired = !WiFiDiagnostics::enabled();
  if (desired != WiFiDiagnostics::enabled()) WiFiDiagnostics::toggle();
  redirect("diagnostics");
}

void handleNetworkSave() {
  const String mode = server.arg("mode");
  if (mode == "dhcp") {
    NetConfig::configureDHCP();
  } else if (mode == "static" && server.hasArg("ip") && server.hasArg("gateway") &&
             server.hasArg("subnet") && server.hasArg("dns1") && server.hasArg("dns2")) {
    NetConfig::saveStatic(server.arg("ip"), server.arg("gateway"),
                          server.arg("subnet"), server.arg("dns1"),
                          server.arg("dns2"));
  }
  WiFiControl::connect();
  redirect("network");
}

void handleNvsFormat() {
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>NVS format requested</h2><p>The ESP32-C3 is erasing NVS and will reboot.</p></body>");
  delay(300);
  const esp_err_t eraseResult = nvs_flash_erase_partition("nvs");
  if (eraseResult == ESP_OK) {
    nvs_flash_init_partition("nvs");
  }
  delay(300);
  ESP.restart();
}

void handleReboot() {
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Rebooting</h2><p>The ESP32-C3 is restarting.</p></body>");
  delay(300);
  ESP.restart();
}

} // namespace

void begin() {
  if (serverStarted) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi/toggle", HTTP_POST, handleToggle);
  server.on("/wifi/reconnect", HTTP_POST, handleReconnect);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/wifi/scan", HTTP_POST, handleWifiScan);
  server.on("/wifi/txpower", HTTP_POST, handleTxPower);
  server.on("/diagnostics/toggle", HTTP_POST, handleDiagnosticsToggle);
  server.on("/network/save", HTTP_POST, handleNetworkSave);
  server.on("/nvs/format", HTTP_POST, handleNvsFormat);
  server.on("/system/reboot", HTTP_POST, handleReboot);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  serverStarted = true;
  Serial.println("Web server started on port 80.");
}

void service() {
  if (!serverStarted) return;
  server.handleClient();
}
}
