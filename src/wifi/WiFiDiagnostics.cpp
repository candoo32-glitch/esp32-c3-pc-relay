#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_event.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "WiFiDiagnostics.h"
#include "../interface/Console.h"
#include "WiFiControl.h"

namespace WiFiDiagnostics {

namespace {
bool diagnosticsEnabled = true;
WiFiEventId_t wifiEventId = 0;
QueueHandle_t wifiDiagnosticQueue = nullptr;
volatile uint32_t activeAttempt = 0;
volatile uint32_t attemptStartMs = 0;
volatile uint32_t eventSequence = 0;
volatile uint32_t droppedEvents = 0;

constexpr size_t DIAGNOSTIC_HISTORY_LINES = 20;
String diagnosticHistory[DIAGNOSTIC_HISTORY_LINES];
size_t diagnosticHistoryCount = 0;
size_t diagnosticHistoryNext = 0;

void rememberDiagnosticLine(const String& line) {
  if (!diagnosticsEnabled || line.isEmpty()) return;
  diagnosticHistory[diagnosticHistoryNext] = line;
  diagnosticHistoryNext = (diagnosticHistoryNext + 1) % DIAGNOSTIC_HISTORY_LINES;
  if (diagnosticHistoryCount < DIAGNOSTIC_HISTORY_LINES) ++diagnosticHistoryCount;
}

String escapeJson(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '\\': escaped += F("\\\\"); break;
      case '"': escaped += F("\\""); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) escaped += ' ';
        else escaped += c;
        break;
    }
  }
  return escaped;
}

String buildRecentLogJson() {
  String json;
  json.reserve(4200);
  json += F("{\"enabled\":");
  json += diagnosticsEnabled ? F("true") : F("false");
  json += F(",\"lines\":[");
  for (size_t i = 0; i < diagnosticHistoryCount; ++i) {
    if (i > 0) json += ',';
    const size_t index =
        (diagnosticHistoryNext + DIAGNOSTIC_HISTORY_LINES - diagnosticHistoryCount + i) %
        DIAGNOSTIC_HISTORY_LINES;
    json += F("\"");
    json += escapeJson(diagnosticHistory[index]);
    json += F("\"");
  }
  json += F("]}");
  return json;
}

struct WiFiDiagnosticRecord {
  uint32_t event = 0;
  uint32_t attempt = 0;
  uint32_t sequence = 0;
  uint32_t elapsedMs = 0;
  uint8_t reason = 0;
  int8_t rssi = -128;
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 0;
  uint8_t authmode = WIFI_AUTH_OPEN;
  uint32_t ip = 0;
  uint32_t gateway = 0;
  uint32_t netmask = 0;
};

} // namespace

const char* wifiDisconnectReasonName(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "DISASSOC_PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "DISASSOC_SUPCHAN_BAD";
    case WIFI_REASON_IE_INVALID: return "IE_INVALID";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
    case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    default: return "UNKNOWN";
  }
}

void wifiArduinoEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
  // Wi-Fi event callbacks execute on a separate FreeRTOS task. Never write
  // terminal output here: even thread-safe Serial calls can interleave with
  // the main task's menu/connection output at character/line boundaries.
  // Copy the event into a fixed-size queue and let the main task render it.
  // When diagnostics are disabled, do not queue events at all.
  if (!diagnosticsEnabled || wifiDiagnosticQueue == nullptr) {
    return;
  }

  WiFiDiagnosticRecord record;
  record.event = static_cast<uint32_t>(event);
  record.attempt = activeAttempt;
  record.sequence = ++eventSequence;
  record.elapsedMs = (record.attempt == 0) ? 0 : (millis() - attemptStartMs);

  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
      const auto& connected = info.wifi_sta_connected;
      record.channel = connected.channel;
      record.authmode = connected.authmode;
      memcpy(record.bssid, connected.bssid, 6);
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      const auto& disconnected = info.wifi_sta_disconnected;
      record.reason = disconnected.reason;
      record.rssi = disconnected.rssi;
      memcpy(record.bssid, disconnected.bssid, 6);
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      const auto& gotIp = info.got_ip;
      record.ip = gotIp.ip_info.ip.addr;
      record.gateway = gotIp.ip_info.gw.addr;
      record.netmask = gotIp.ip_info.netmask.addr;
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      break;

    default:
      return;
  }

  // This is a callback task, not an ISR. A zero-timeout send guarantees the
  // Wi-Fi event task can never block on terminal I/O or a full queue.
  if (xQueueSend(wifiDiagnosticQueue, &record, 0) != pdTRUE) {
    ++droppedEvents;
  }
}

void beginConnectionAttempt() {
  if (!diagnosticsEnabled || wifiDiagnosticQueue == nullptr) return;

  WiFiDiagnosticRecord stale;
  uint32_t drained = 0;
  while (xQueueReceive(wifiDiagnosticQueue, &stale, 0) == pdTRUE) {
    ++drained;
  }

  ++activeAttempt;
  if (activeAttempt == 0) ++activeAttempt;
  attemptStartMs = millis();
  droppedEvents = 0;

  char summary[96];
  snprintf(summary, sizeof(summary),
           "ATTEMPT=%lu QUEUE_DRAINED=%lu",
           static_cast<unsigned long>(activeAttempt),
           static_cast<unsigned long>(drained));
  printText(summary, "1;37m");
}

void service() {
  if (!diagnosticsEnabled || wifiDiagnosticQueue == nullptr) return;

  auto diagnosticColor = [](const char* code) {
    if (Console::ansiSupported) Console::color(code);
  };
  auto diagnosticReset = []() {
    if (Console::ansiSupported) Console::resetStyle();
  };
  auto printPrefix = [&]() {
    diagnosticColor("1;36m");
    Serial.print("WIFI");
    diagnosticReset();
    Serial.print(" | ");
  };
  auto printSection = [&](const char* code, const char* text) {
    diagnosticColor(code);
    Serial.print(text);
    diagnosticReset();
  };

  WiFiDiagnosticRecord record;
  while (xQueueReceive(wifiDiagnosticQueue, &record, 0) == pdTRUE) {
    switch (record.event) {
      case ARDUINO_EVENT_WIFI_STA_START: {
        char meta[64];
        snprintf(meta, sizeof(meta), "A=%lu E=%lu +%lums",
                 static_cast<unsigned long>(record.attempt),
                 static_cast<unsigned long>(record.sequence),
                 static_cast<unsigned long>(record.elapsedMs));
        printPrefix(); printSection("1;37m", meta); Serial.print(" | ");
        printSection("1;33m", "START"); Serial.print("\r\n");
        rememberDiagnosticLine(String("WIFI | ") + meta + " | START");
        break;
      }
      case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
        const char* auth = WiFiControl::authModeName(static_cast<wifi_auth_mode_t>(record.authmode));
        char meta[64], bssid[18];
        snprintf(meta, sizeof(meta), "A=%lu E=%lu +%lums",
                 static_cast<unsigned long>(record.attempt),
                 static_cast<unsigned long>(record.sequence),
                 static_cast<unsigned long>(record.elapsedMs));
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 record.bssid[0], record.bssid[1], record.bssid[2],
                 record.bssid[3], record.bssid[4], record.bssid[5]);
        printPrefix(); printSection("1;37m", meta); Serial.print(" | ");
        printSection("1;32m", "CONNECTED"); Serial.print(" | CH=");
        printSection("1;36m", String(record.channel).c_str()); Serial.print(" | AUTH=");
        printSection("1;33m", auth); Serial.print(" | BSSID=");
        printSection("1;35m", bssid); Serial.print("\r\n");
        rememberDiagnosticLine(String("WIFI | ") + meta + " | CONNECTED | CH=" +
                               String(record.channel) + " | AUTH=" + auth + " | BSSID=" + bssid);
        break;
      }
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const char* reason = wifiDisconnectReasonName(record.reason);
        char meta[64], reasonNumber[4], rssi[8], bssid[18];
        snprintf(meta, sizeof(meta), "A=%lu E=%lu +%lums",
                 static_cast<unsigned long>(record.attempt),
                 static_cast<unsigned long>(record.sequence),
                 static_cast<unsigned long>(record.elapsedMs));
        snprintf(reasonNumber, sizeof(reasonNumber), "%u", record.reason);
        snprintf(rssi, sizeof(rssi), "%d", record.rssi);
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 record.bssid[0], record.bssid[1], record.bssid[2],
                 record.bssid[3], record.bssid[4], record.bssid[5]);
        printPrefix(); printSection("1;37m", meta); Serial.print(" | ");
        printSection("1;31m", "DISCONNECTED"); Serial.print(" | R=");
        printSection("1;33m", reasonNumber); Serial.print(" ");
        printSection("1;31m", reason); Serial.print(" | RSSI=");
        printSection("1;35m", rssi); Serial.print(" | BSSID=");
        printSection("1;34m", bssid); Serial.print("\r\n");
        rememberDiagnosticLine(String("WIFI | ") + meta + " | DISCONNECTED | R=" +
                               reasonNumber + " " + reason + " | RSSI=" + rssi +
                               " | BSSID=" + bssid);
        break;
      }
      case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        const String ip = IPAddress(record.ip).toString();
        const String gateway = IPAddress(record.gateway).toString();
        const String netmask = IPAddress(record.netmask).toString();
        char meta[64];
        snprintf(meta, sizeof(meta), "A=%lu E=%lu +%lums",
                 static_cast<unsigned long>(record.attempt),
                 static_cast<unsigned long>(record.sequence),
                 static_cast<unsigned long>(record.elapsedMs));
        printPrefix(); printSection("1;37m", meta); Serial.print(" | ");
        printSection("1;32m", "GOT_IP"); Serial.print(" | IP=");
        printSection("1;32m", ip.c_str()); Serial.print(" | GW=");
        printSection("1;36m", gateway.c_str()); Serial.print(" | MASK=");
        printSection("1;33m", netmask.c_str()); Serial.print("\r\n");
        rememberDiagnosticLine(String("WIFI | ") + meta + " | GOT_IP | IP=" +
                               ip + " | GW=" + gateway + " | MASK=" + netmask);
        break;
      }
      case ARDUINO_EVENT_WIFI_STA_LOST_IP: {
        char meta[64];
        snprintf(meta, sizeof(meta), "A=%lu E=%lu +%lums",
                 static_cast<unsigned long>(record.attempt),
                 static_cast<unsigned long>(record.sequence),
                 static_cast<unsigned long>(record.elapsedMs));
        printPrefix(); printSection("1;37m", meta); Serial.print(" | ");
        printSection("1;31m", "LOST_IP"); Serial.print("\r\n");
        rememberDiagnosticLine(String("WIFI | ") + meta + " | LOST_IP");
        break;
      }
      default: break;
    }
  }
}

void printLine(const char* label, const char* value,
               const char* labelColor,
               const char* valueColor) {
  if (!diagnosticsEnabled) return;
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.print("WIFI | ");
  if (Console::ansiSupported) Console::color(labelColor);
  Serial.print(label);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.print("=");
  if (Console::ansiSupported) Console::color(valueColor);
  Serial.print(value);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.print("\r\n");
  rememberDiagnosticLine(String("WIFI | ") + label + "=" + value);
}

void printText(const char* text, const char* textColor) {
  if (!diagnosticsEnabled) return;
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.print("WIFI | ");
  if (Console::ansiSupported) Console::color(textColor);
  Serial.print(text);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.print("\r\n");
  rememberDiagnosticLine(String("WIFI | ") + text);
}

void printFailure(const char* label, const char* errorName) {
  if (!diagnosticsEnabled) return;
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.print("WIFI | ");
  if (Console::ansiSupported) Console::color("1;31m");
  Serial.print(label);
  Serial.print("=");
  Serial.print(errorName);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.print("\r\n");
  rememberDiagnosticLine(String("WIFI | ") + label + "=" + errorName);
}

void begin() {
  diagnosticsEnabled = WiFiControl::diagnosticsPreference();

  wifiDiagnosticQueue = xQueueCreate(32, sizeof(WiFiDiagnosticRecord));
  if (wifiDiagnosticQueue == nullptr) {
    if (diagnosticsEnabled) {
      if (Console::ansiSupported) Console::color("1;31m");
      Serial.println("WARNING: WiFi diagnostic queue allocation failed.");
      if (Console::ansiSupported) Console::resetStyle();
    }
    return;
  }

  // Use Arduino-ESP32's public WiFi event API so diagnostics follow the
  // same event translation used internally by the STA implementation.
  wifiEventId = WiFi.onEvent(wifiArduinoEventHandler);
}

bool enabled() {
  return diagnosticsEnabled;
}

String recentLogJson() {
  return buildRecentLogJson();
}

void printMenuSetting() {
  if (Console::ansiSupported) {
    Console::color(diagnosticsEnabled ? "1;32m" : "1;31m");
  }
  Serial.print("6. Diagnostics: ");
  Serial.println(diagnosticsEnabled ? "ON" : "OFF");
  if (Console::ansiSupported) {
    Console::resetStyle();
  }
}

void toggle() {
  diagnosticsEnabled = !diagnosticsEnabled;
  WiFiControl::saveDiagnosticsPreference(diagnosticsEnabled);

  if (Console::ansiSupported) {
    Console::color(diagnosticsEnabled ? "1;32m" : "1;31m");
  }
  Serial.print("WiFi diagnostics: ");
  Serial.println(diagnosticsEnabled ? "ON" : "OFF");
  if (Console::ansiSupported) {
    Console::resetStyle();
  }

  // Discard any queued events at the toggle boundary so changing the setting
  // never causes stale diagnostics to appear under the new state.
  if (wifiDiagnosticQueue != nullptr) {
    WiFiDiagnosticRecord record;
    while (xQueueReceive(wifiDiagnosticQueue, &record, 0) == pdTRUE) {}
  }
}

} // namespace WiFiDiagnostics
