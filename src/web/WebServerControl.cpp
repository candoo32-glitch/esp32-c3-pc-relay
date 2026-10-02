#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include "WebServerControl.h"
#include "../wifi/WiFiControl.h"

namespace WebControl {
namespace {
WebServer server(80);

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
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

String page() {
  const bool enabled = WiFiControl::isEnabled();
  const bool connected = enabled && WiFi.status() == WL_CONNECTED;
  const uint32_t uptime = millis() / 1000UL;
  int8_t txPower = 0;
  const esp_err_t txResult = esp_wifi_get_max_tx_power(&txPower);

  String html;
  html.reserve(6000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32-C3 Relay</title><style>");
  html += F("body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:20px;max-width:760px;margin:auto}");
  html += F("h1{font-size:24px;margin:0 0 6px}h2{font-size:16px;margin:22px 0 8px}");
  html += F(".card{background:#1d1d1d;border:1px solid #444;border-radius:10px;padding:16px;margin:12px 0}");
  html += F(".ok{color:#59d36c}.warn{color:#f1c75b}.bad{color:#ff6b6b}.muted{color:#aaa}");
  html += F("table{width:100%;border-collapse:collapse}td{padding:7px 4px;border-bottom:1px solid #333}td:first-child{color:#aaa;width:42%}");
  html += F("button{font-size:16px;padding:10px 16px;border:0;border-radius:7px;margin:4px 4px 4px 0;cursor:pointer}");
  html += F(".on{background:#276b35;color:white}.off{background:#7b3030;color:white}.action{background:#315a88;color:white}");
  html += F("</style></head><body><h1>ESP32-C3 PC Relay</h1>");
  html += F("<div class='muted'>Headless verification dashboard</div>");
  html += F("<div class='card'><h2>Wi-Fi</h2><div class='");
  html += connected ? F("ok'>CONNECTED") : (enabled ? F("warn'>DISCONNECTED") : F("muted'>OFF"));
  html += F("</div><table>");
  html += F("<tr><td>Power</td><td>"); html += enabled ? F("ON") : F("OFF"); html += F("</td></tr>");
  html += F("<tr><td>SSID</td><td>"); html += connected ? htmlEscape(WiFi.SSID()) : F("—"); html += F("</td></tr>");
  html += F("<tr><td>IP address</td><td>"); html += connected ? WiFi.localIP().toString() : F("—"); html += F("</td></tr>");
  html += F("<tr><td>Gateway</td><td>"); html += connected ? WiFi.gatewayIP().toString() : F("—"); html += F("</td></tr>");
  html += F("<tr><td>RSSI</td><td>"); html += connected ? String(WiFi.RSSI()) + F(" dBm") : F("—"); html += F("</td></tr>");
  html += F("<tr><td>Channel</td><td>"); html += connected ? String(WiFi.channel()) : F("—"); html += F("</td></tr>");
  html += F("<tr><td>BSSID</td><td>"); html += connected ? htmlEscape(WiFi.BSSIDstr()) : F("—"); html += F("</td></tr>");
  html += F("<tr><td>TX power</td><td>");
  if (txResult == ESP_OK) html += String(static_cast<float>(txPower) * 0.25f, 2) + F(" dBm"); else html += F("unavailable");
  html += F("</td></tr></table>");
  html += F("<form method='POST' action='/wifi/toggle'><button class='");
  html += enabled ? F("off'>Turn Wi-Fi OFF") : F("on'>Turn Wi-Fi ON");
  html += F("</button></form>");
  if (enabled) html += F("<form method='POST' action='/wifi/reconnect'><button class='action'>Reconnect now</button></form>");
  html += F("</div>");
  html += F("<div class='card'><h2>Firmware</h2><table>");
  html += F("<tr><td>Uptime</td><td>"); html += String(uptime); html += F(" seconds</td></tr>");
  html += F("<tr><td>ESP-IDF</td><td>"); html += esp_get_idf_version(); html += F("</td></tr>");
  html += F("<tr><td>Build</td><td>");
#ifdef FW_BUILD_VERSION
  html += String(FW_BUILD_VERSION);
#else
  html += F("0");
#endif
  html += F("</td></tr></table></div>");
  html += F("<div class='muted'>Page status: "); html += statusText(); html += F("</div>");
  html += F("</body></html>");
  return html;
}

void handleRoot() { server.send(200, "text/html; charset=utf-8", page()); }

void handleToggle() {
  WiFiControl::setEnabled(!WiFiControl::isEnabled());
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "Redirecting");
}

void handleReconnect() {
  WiFiControl::connect();
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "Reconnecting");
}
}

void begin() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) return;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi/toggle", HTTP_POST, handleToggle);
  server.on("/wifi/reconnect", HTTP_POST, handleReconnect);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println("Web server started on port 80.");
}

void service() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) return;
  server.handleClient();
}
}
