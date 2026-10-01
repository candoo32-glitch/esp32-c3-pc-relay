#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>
#include "esp_log.h"
#include "esp_arduino_version.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "WiFiControl.h"
#include "../interface/Console.h"
#include "../network/NetConfig.h"

namespace WiFiControl {
Preferences preferences;

volatile uint8_t lastDisconnectReason = 0;
volatile int8_t lastDisconnectRSSI = 0;
volatile uint8_t disconnectEventCount = 0;
volatile uint8_t lastDisconnectBSSID[6] = {0, 0, 0, 0, 0, 0};
portMUX_TYPE wifiEventTraceMux = portMUX_INITIALIZER_UNLOCKED;

struct WiFiEventTraceEntry {
  uint32_t elapsedMs;
  arduino_event_id_t eventId;
  uint8_t reason;
};

constexpr uint8_t WIFI_EVENT_TRACE_MAX = 24;
volatile uint8_t wifiEventTraceCount = 0;
uint32_t wifiEventTraceStartMs = 0;
WiFiEventTraceEntry wifiEventTrace[WIFI_EVENT_TRACE_MAX];

const char* wifiEventName(arduino_event_id_t eventId) {
  switch (eventId) {
    case ARDUINO_EVENT_WIFI_STA_START: return "STA_START";
    case ARDUINO_EVENT_WIFI_STA_STOP: return "STA_STOP";
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: return "STA_CONNECTED";
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: return "STA_DISCONNECTED";
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE: return "STA_AUTHMODE_CHANGE";
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: return "STA_GOT_IP";
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6: return "STA_GOT_IP6";
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: return "STA_LOST_IP";
    default: return "OTHER";
  }
}

void resetWiFiEventTrace() {
  portENTER_CRITICAL(&wifiEventTraceMux);
  wifiEventTraceCount = 0;
  wifiEventTraceStartMs = millis();
  memset((void*)wifiEventTrace, 0, sizeof(wifiEventTrace));
  portEXIT_CRITICAL(&wifiEventTraceMux);
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  const arduino_event_id_t eventId =
      static_cast<arduino_event_id_t>(event);

  portENTER_CRITICAL(&wifiEventTraceMux);

  if (wifiEventTraceCount < WIFI_EVENT_TRACE_MAX) {
    const uint8_t index = wifiEventTraceCount++;
    wifiEventTrace[index].elapsedMs = millis() - wifiEventTraceStartMs;
    wifiEventTrace[index].eventId = eventId;
    wifiEventTrace[index].reason =
        eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED
            ? info.wifi_sta_disconnected.reason
            : 0;
  }

  if (eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
    lastDisconnectRSSI = info.wifi_sta_disconnected.rssi;
    memcpy((void*)lastDisconnectBSSID, info.wifi_sta_disconnected.bssid, 6);
    ++disconnectEventCount;
  }

  portEXIT_CRITICAL(&wifiEventTraceMux);
}

const char* authModeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK: return "WAPI-PSK";
    case WIFI_AUTH_OWE: return "OWE";
    case WIFI_AUTH_WPA3_ENT_192: return "WPA3-ENT-192";
    case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3-ENT";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/WPA3-ENT";
    case WIFI_AUTH_WPA_ENTERPRISE: return "WPA-ENT";
    case WIFI_AUTH_DPP: return "DPP";
    default: return "UNKNOWN";
  }
}

void printWPA3Support() {
#ifdef CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE
  Serial.println("WPA3 SAE support: compiled in");
#else
  Serial.println("WPA3 SAE support: NOT compiled in");
#endif
}

constexpr char PREF_NAMESPACE[] = "wifi";
constexpr char SSID_KEY[] = "ssid";
constexpr char PASSWORD_KEY[] = "password";
constexpr char HOSTNAME[] = "esp32-c3-relay";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

const char* disconnectReasonName(uint8_t reason) {
  const char* name =
      WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason));
  return (name != nullptr && *name != '\0') ? name : "UNKNOWN";
}

void resetConnectionDiagnostics() {
  portENTER_CRITICAL(&wifiEventTraceMux);
  wifiEventTraceCount = 0;
  memset((void*)wifiEventTrace, 0, sizeof(wifiEventTrace));
  lastDisconnectReason = 0;
  lastDisconnectRSSI = 0;
  disconnectEventCount = 0;
  memset((void*)lastDisconnectBSSID, 0, sizeof(lastDisconnectBSSID));
  portEXIT_CRITICAL(&wifiEventTraceMux);
}

void printDriverVersion() {
  Serial.printf("Arduino-ESP32: %s\n", ESP_ARDUINO_VERSION_STR);
  Serial.printf("ESP-IDF:       %s\n", esp_get_idf_version());
}

void configureVerboseWiFiLogging() {
  // ESP-IDF documents per-component runtime log control. We deliberately use
  // only the public logging API; no internal Wi-Fi logging functions.
  esp_log_level_set("wifi", ESP_LOG_VERBOSE);
  esp_log_level_set("wpa", ESP_LOG_DEBUG);
}

void printWiFiEventTrace() {
  uint8_t traceCount;
  WiFiEventTraceEntry trace[WIFI_EVENT_TRACE_MAX];

  portENTER_CRITICAL(&wifiEventTraceMux);
  traceCount = wifiEventTraceCount;
  memcpy(trace, (const void*)wifiEventTrace, sizeof(trace));
  portEXIT_CRITICAL(&wifiEventTraceMux);

  Serial.println();
  Serial.println("WiFi event sequence");
  Serial.println("-------------------");

  if (traceCount == 0) {
    Serial.println("No Arduino WiFi events captured.");
    return;
  }

  for (uint8_t i = 0; i < traceCount; ++i) {
    Serial.print("  +");
    Serial.print(trace[i].elapsedMs);
    Serial.print(" ms  ");
    Serial.print(wifiEventName(trace[i].eventId));

    if (trace[i].eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.print("  reason=");
      Serial.print(trace[i].reason);
      Serial.print(" (");
      Serial.print(disconnectReasonName(trace[i].reason));
      Serial.print(")");
    }

    Serial.println();
  }

  bool sawStart = false;
  bool sawConnected = false;
  bool sawAuthModeChange = false;
  bool sawGotIP = false;

  for (uint8_t i = 0; i < traceCount; ++i) {
    switch (trace[i].eventId) {
      case ARDUINO_EVENT_WIFI_STA_START:
        sawStart = true;
        break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        sawConnected = true;
        break;
      case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        sawAuthModeChange = true;
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        sawGotIP = true;
        break;
      default:
        break;
    }
  }

  Serial.println();
  Serial.print("  STA_START seen:            ");
  Serial.println(sawStart ? "YES" : "NO");
  Serial.print("  STA_CONNECTED seen:        ");
  Serial.println(sawConnected ? "YES" : "NO");
  Serial.print("  STA_AUTHMODE_CHANGE seen:  ");
  Serial.println(sawAuthModeChange ? "YES" : "NO");
  Serial.print("  STA_GOT_IP seen:            ");
  Serial.println(sawGotIP ? "YES" : "NO");
}

void printConnectionDiagnostics() {
  printWiFiEventTrace();

  Serial.println();
  Serial.println("Connection diagnostics");
  Serial.println("----------------------");

  Serial.print("Final WiFi status: ");
  Serial.println(static_cast<int>(WiFi.status()));

  bool hadDisconnect;
  portENTER_CRITICAL(&wifiEventTraceMux);
  hadDisconnect = disconnectEventCount != 0;
  portEXIT_CRITICAL(&wifiEventTraceMux);

  if (hadDisconnect) {
    uint8_t reason;
    int8_t rssi;
    uint8_t bssid[6];

    portENTER_CRITICAL(&wifiEventTraceMux);
    reason = lastDisconnectReason;
    rssi = lastDisconnectRSSI;
    memcpy(bssid, (const void*)lastDisconnectBSSID, sizeof(bssid));
    portEXIT_CRITICAL(&wifiEventTraceMux);

    Serial.print("Last disconnect reason: ");
    Serial.print(static_cast<unsigned>(reason));
    Serial.print(" (");
    Serial.print(disconnectReasonName(reason));
    Serial.print("), RSSI ");
    Serial.print(static_cast<int>(rssi));
    Serial.println(" dBm");

    Serial.printf("Disconnect BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bssid[0], bssid[1], bssid[2],
                  bssid[3], bssid[4], bssid[5]);

    if (reason == 2) {
      Serial.println("Meaning: authentication expired.");
      Serial.println("ESP-IDF defines this as an authentication timeout OR");
      Serial.println("a reason code received from the AP.");
      Serial.println("Password validity: NOT DETERMINED.");
    } else if (reason == 15 || reason == 23 ||
               reason == 24 || reason == 34) {
      Serial.println("Meaning: failure occurred after authentication during");
      Serial.println("the WPA/802.1X handshake.");
      Serial.println("Password validity: not treated as proven by this firmware.");
    }
  } else {
    Serial.println("No STA_DISCONNECTED event was captured.");
  }
}

struct TargetAP {
  bool valid = false;
  int32_t channel = 0;
  int8_t rssi = -127;
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
};

void printTargetAP(const TargetAP& target) {
  if (!target.valid) {
    Serial.println("      Target AP: none");
    return;
  }

  Serial.printf(
      "      Target AP: %02X:%02X:%02X:%02X:%02X:%02X  CH %ld  RSSI %d dBm  %s\n",
      target.bssid[0], target.bssid[1], target.bssid[2],
      target.bssid[3], target.bssid[4], target.bssid[5],
      static_cast<long>(target.channel),
      static_cast<int>(target.rssi),
      authModeName(target.auth));
}

enum class ConnectResult : uint8_t {
  SUCCESS,
  SSID_NOT_FOUND,
  AUTH_EXPIRED,
  AUTH_FAILED,
  HANDSHAKE_FAILED,
  CONNECTION_LOST,
  TIMEOUT,
  NETWORK_CONFIG_FAILED,
  SERIAL_DISCONNECTED,
  UNKNOWN
};

const char* connectResultName(ConnectResult result) {
  switch (result) {
    case ConnectResult::SUCCESS: return "SUCCESS";
    case ConnectResult::SSID_NOT_FOUND: return "SSID_NOT_FOUND";
    case ConnectResult::AUTH_EXPIRED: return "AUTH_EXPIRED";
    case ConnectResult::AUTH_FAILED: return "AUTH_FAILED";
    case ConnectResult::HANDSHAKE_FAILED: return "HANDSHAKE_FAILED";
    case ConnectResult::CONNECTION_LOST: return "CONNECTION_LOST";
    case ConnectResult::TIMEOUT: return "TIMEOUT";
    case ConnectResult::NETWORK_CONFIG_FAILED: return "NETWORK_CONFIG_FAILED";
    case ConnectResult::SERIAL_DISCONNECTED: return "SERIAL_DISCONNECTED";
    default: return "UNKNOWN";
  }
}

ConnectResult classifyConnectionFailure(uint32_t elapsedMs) {
  uint8_t reason = 0;

  portENTER_CRITICAL(&wifiEventTraceMux);
  if (disconnectEventCount != 0) {
    reason = lastDisconnectReason;
  }
  portEXIT_CRITICAL(&wifiEventTraceMux);

  switch (reason) {
    case 2:
      return ConnectResult::AUTH_EXPIRED;
    case 15:
    case 23:
    case 24:
    case 34:
      return ConnectResult::HANDSHAKE_FAILED;
    case 3:
    case 8:
      return ConnectResult::CONNECTION_LOST;
    default:
      break;
  }

  const wl_status_t status = WiFi.status();

  if (status == WL_NO_SSID_AVAIL) {
    return ConnectResult::SSID_NOT_FOUND;
  }

  if (status == WL_CONNECTION_LOST) {
    return ConnectResult::CONNECTION_LOST;
  }

  if (elapsedMs >= CONNECT_TIMEOUT_MS) {
    return ConnectResult::TIMEOUT;
  }

  if (status == WL_CONNECT_FAILED) {
    return ConnectResult::AUTH_FAILED;
  }

  return ConnectResult::UNKNOWN;
}

ConnectResult startExplicitStation(const String& ssid,
                                  const String& password,
                                  const TargetAP* requestedTarget,
                                  wifi_auth_mode_t minSecurity) {
  const int32_t channel =
      requestedTarget != nullptr && requestedTarget->valid
          ? requestedTarget->channel : 0;
  const uint8_t* bssid =
      requestedTarget != nullptr && requestedTarget->valid
          ? requestedTarget->bssid : nullptr;

  const wl_status_t status = WiFi.begin(
      ssid.c_str(),
      password.isEmpty() ? nullptr : password.c_str(),
      channel, bssid, false);

  if (status == WL_CONNECT_FAILED) {
    Serial.println("ERROR: Arduino STA initialization/configuration failed.");
    return ConnectResult::UNKNOWN;
  }

  wifi_config_t config;
  memset(&config, 0, sizeof(config));
  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: esp_wifi_get_config failed: 0x%x (%s)\\n",
                  err, esp_err_to_name(err));
    return ConnectResult::UNKNOWN;
  }

  // Explicitly set the authentication threshold, including OPEN networks.
  config.sta.threshold.authmode = minSecurity;
  config.sta.threshold.rssi = -127;
  config.sta.scan_method = WIFI_FAST_SCAN;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;

  if (requestedTarget != nullptr && requestedTarget->valid) {
    config.sta.channel = requestedTarget->channel;
    config.sta.bssid_set = 1;
    memcpy(config.sta.bssid, requestedTarget->bssid, 6);
  } else {
    config.sta.channel = 0;
    config.sta.bssid_set = 0;
    memset(config.sta.bssid, 0, sizeof(config.sta.bssid));
  }

  // Arduino-ESP32 4.0.0-RC1's bundled ESP-IDF 6.1 headers do not expose
  // disable_wpa3_compatible_mode in wifi_sta_config_t. Do not write fields
  // that are only present in newer IDF headers.

  err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: esp_wifi_set_config failed: 0x%x (%s)\\n",
                  err, esp_err_to_name(err));
    return ConnectResult::UNKNOWN;
  }

  wifi_config_t verify;
  memset(&verify, 0, sizeof(verify));
  err = esp_wifi_get_config(WIFI_IF_STA, &verify);
  if (err != ESP_OK) {
    Serial.printf("ERROR: station config verification failed: 0x%x (%s)\\n",
                  err, esp_err_to_name(err));
    return ConnectResult::UNKNOWN;
  }

  Serial.println("Explicit IDF 6.1 station configuration:");
  Serial.print("  auth threshold: ");
  Serial.println(authModeName(verify.sta.threshold.authmode));
  Serial.print("  channel:        ");
  Serial.println(verify.sta.channel);
  Serial.print("  BSSID pinned:   ");
  Serial.println(verify.sta.bssid_set ? "YES" : "NO");
  Serial.print("  PMF required:   ");
  Serial.println(verify.sta.pmf_cfg.required ? "YES" : "NO");
  Serial.print("  WPA3 override:  ");
  Serial.println(verify.sta.disable_wpa3_compatible_mode ? "DISABLED" : "ENABLED");

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    Serial.printf("ERROR: esp_wifi_connect failed: 0x%x (%s)\\n",
                  err, esp_err_to_name(err));
    return ConnectResult::UNKNOWN;
  }

  return ConnectResult::SUCCESS;
}

ConnectResult connectWithCredentials(const String& ssid,
                                      const String& password,
                                      const TargetAP* requestedTarget = nullptr) {
  if (ssid.isEmpty()) {
    Serial.println("WiFi: not configured.");
    return ConnectResult::SSID_NOT_FOUND;
  }

  const bool previousAutoReconnect = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false);

  /*
   * This routine deliberately uses the public Arduino-ESP32 4.0.0-RC1 STA API.
   *
   * 3.3.12 is built on ESP-IDF 6.1 in this project. The documented
   * WiFi.begin() overload maps directly to the driver's station configuration
   * and esp_wifi_connect() path. We do not manufacture a second
   * wifi_config_t here, and we do not call private/internal Wi-Fi functions.
   *
   * A scan-selected TargetAP supplies an exact channel+BSSID. A normal saved
   * connection supplies neither; with FAST_SCAN the driver uses the first
   * matching BSS it finds for the SSID.
   */
  Serial.println();
  Serial.println("WiFi connection");
  Serial.println("----------------");
  printDriverVersion();

  Serial.print("SSID:      ");
  Serial.println(ssid);
  Serial.print("Mode:      ");
  Serial.println(NetConfig::mode() == NetConfig::Mode::STATIC ? "STATIC" : "DHCP");
  Serial.print("Password:  ");
  Serial.println(password.isEmpty() ? "none (open network)" : "configured");
  Serial.print("STA MAC:   ");
  Serial.println(WiFi.macAddress());

  if (requestedTarget != nullptr && requestedTarget->valid) {
    Serial.println("Target:    exact scanned BSSID + channel");
    printTargetAP(*requestedTarget);
  } else {
    Serial.println("Target:    SSID only; driver selects the BSS");
  }

  // These are public Arduino-ESP32 STA controls. They must be configured
  // before WiFi.begin(), exactly as documented by the 3.3.12 API.
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_FAST_SCAN);

  // Do not infer the AP security level from "password present". When a scan
  // selected the AP, use the security mode actually reported by that scan.
  // For a manually entered/saved password, allow WPA and newer personal
  // security modes while still excluding WEP.
  wifi_auth_mode_t minSecurity = WIFI_AUTH_OPEN;
  if (requestedTarget != nullptr && requestedTarget->valid) {
    minSecurity = requestedTarget->auth;
  } else if (!password.isEmpty()) {
    minSecurity = WIFI_AUTH_WPA_PSK;
  }
  WiFi.setMinSecurity(minSecurity);

  if (!NetConfig::apply()) {
    Serial.println("ERROR: network/IP configuration failed.");
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }

  Serial.println();
  Serial.println("Starting station connection...");
  Serial.println("  Driver storage: RAM");
  Serial.println("  Auto-reconnect: disabled");
  Serial.println("  Scan method:    FAST");
  if (requestedTarget != nullptr && requestedTarget->valid) {
    Serial.println("  AP selection:   pinned BSSID + channel");
  } else {
    Serial.println("  AP selection:   FAST scan, first SSID match");
  }
  Serial.print("  Minimum security: ");
  Serial.println(authModeName(minSecurity));

  configureVerboseWiFiLogging();

  /*
   * Do not power-cycle the Wi-Fi driver between attempts. The Arduino core's
   * disconnect(false, false) means "disconnect the STA, leave Wi-Fi running,
   * do not erase the AP configuration". That is exactly the clean pre-connect
   * state we want.
   */
  if (!WiFi.disconnect(false, false, 1000)) {
    Serial.println("WARNING: STA disconnect-before-connect did not complete.");
  }

  // Start a clean diagnostic window only after the deliberate disconnect
  // above, so that cleanup events cannot be mistaken for AP failures.
  resetConnectionDiagnostics();

  wl_status_t beginStatus;

  if (requestedTarget != nullptr && requestedTarget->valid) {
    beginStatus = startExplicitStation(ssid, password, requestedTarget, minSecurity) == ConnectResult::SUCCESS
                      ? WL_CONNECTED : WL_CONNECT_FAILED;
  } else {
    beginStatus = startExplicitStation(ssid, password, nullptr, minSecurity) == ConnectResult::SUCCESS
                      ? WL_CONNECTED : WL_CONNECT_FAILED;
  }

  if (beginStatus == WL_CONNECT_FAILED) {
    Serial.println("ERROR: WiFi.begin() rejected the station configuration.");
    printConnectionDiagnostics();
    WiFi.disconnect(false, false, 1000);
    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::UNKNOWN;
  }

  Serial.printf("WiFi.begin() returned status: %d (%s)\n",
                static_cast<int>(beginStatus),
                beginStatus == WL_CONNECTED ? "WL_CONNECTED" :
                beginStatus == WL_CONNECT_FAILED ? "WL_CONNECT_FAILED" :
                beginStatus == WL_NO_SSID_AVAIL ? "WL_NO_SSID_AVAIL" :
                beginStatus == WL_DISCONNECTED ? "WL_DISCONNECTED" :
                "OTHER");
  Serial.println("Waiting for STA_CONNECTED and STA_GOT_IP...");
  Serial.println();

  const uint32_t startTime = millis();
  uint32_t lastStatusReport = startTime;
  wl_status_t lastStatus = WiFi.status();

  while (millis() - startTime < CONNECT_TIMEOUT_MS) {
    if (Console::disconnected()) {
      Serial.println("Serial session disconnected; cancelling WiFi attempt.");
      WiFi.disconnect(false, false, 1000);
      WiFi.setAutoReconnect(previousAutoReconnect);
      Console::prepareForMenuInput();
      return ConnectResult::SERIAL_DISCONNECTED;
    }

    const wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
      break;
    }

    if (status != lastStatus || millis() - lastStatusReport >= 2000) {
      Serial.print("  status=");
      Serial.print(static_cast<int>(status));
      Serial.print("  elapsed=");
      Serial.print((millis() - startTime) / 1000);
      Serial.println("s");
      lastStatus = status;
      lastStatusReport = millis();
    }

    delay(100);
  }

  const uint32_t elapsedMs = millis() - startTime;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connection SUCCESSFUL");
    Serial.print("  SSID:    ");
    Serial.println(WiFi.SSID());
    Serial.print("  BSSID:   ");
    Serial.println(WiFi.BSSIDstr());
    Serial.print("  Channel: ");
    Serial.println(WiFi.channel());
    Serial.print("  RSSI:    ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    printStatus();

    // Credentials are application-owned. Persist only after the driver and
    // network interface have both reached a successful connected state.
    preferences.putString(SSID_KEY, ssid);
    preferences.putString(PASSWORD_KEY, password);
    Serial.println("WiFi credentials saved.");

    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::SUCCESS;
  }

  Serial.println();
  Serial.println("WiFi connection FAILED");
  Serial.print("Elapsed: ");
  Serial.print(elapsedMs);
  Serial.println(" ms");

  printConnectionDiagnostics();

  const ConnectResult result = classifyConnectionFailure(elapsedMs);

  Serial.print("Connection result: ");
  Serial.println(connectResultName(result));
  Serial.println("Credentials were NOT changed.");

  // Stop any internal retry activity before returning control to the menu.
  // Do not erase the driver configuration: the next WiFi.begin() call owns
  // and replaces it, while persistent(false) keeps it out of Wi-Fi NVS.
  WiFi.disconnect(false, false, 1000);
  WiFi.setAutoReconnect(false);
  WiFi.setAutoReconnect(previousAutoReconnect);

  Console::prepareForMenuInput();
  return result;
}
void printStatus() {
  Serial.println();
  Serial.println("WiFi status");
  Serial.println("-----------");

  Serial.print("State: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("CONNECTED");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.print("NOT CONNECTED (status=");
    Serial.print(static_cast<int>(WiFi.status()));
    Serial.println(")");
  }

  Serial.println();
}

bool connect() {
  const String ssid = preferences.getString(SSID_KEY, "");
  const String password = preferences.getString(PASSWORD_KEY, "");
  return connectWithCredentials(ssid, password) == ConnectResult::SUCCESS;
}

// Terminal table geometry.
// The rendered table is exactly 78 columns wide, safely within the common
// 80-column terminal limit.
constexpr size_t TABLE_WIDTH = 78;
constexpr size_t NUMBER_WIDTH = 3;
constexpr size_t SSID_WIDTH = 22;
constexpr size_t RSSI_WIDTH = 6;
constexpr size_t CHANNEL_WIDTH = 2;
constexpr size_t SECURITY_WIDTH = 9;
constexpr size_t BSSID_WIDTH = 17;

static_assert(
    NUMBER_WIDTH + SSID_WIDTH + RSSI_WIDTH + CHANNEL_WIDTH +
    SECURITY_WIDTH + BSSID_WIDTH + 19 == TABLE_WIDTH,
    "WiFi scan table geometry must remain fixed");

void printGridSeparator() {
  Serial.println("+-----+------------------------+--------+----+-----------+-------------------+");
}

constexpr size_t MAX_UNIQUE_SSIDS = 128;

int buildUniqueSSIDList(int scanCount, int* representatives, int maxEntries) {
  int uniqueCount = 0;

  for (int i = 0; i < scanCount; ++i) {
    const String ssid = WiFi.SSID(i);

    int existing = -1;
    for (int u = 0; u < uniqueCount; ++u) {
      if (WiFi.SSID(representatives[u]) == ssid) {
        existing = u;
        break;
      }
    }

    if (existing >= 0) {
      // Keep the strongest AP for this SSID.
      if (WiFi.RSSI(i) > WiFi.RSSI(representatives[existing])) {
        representatives[existing] = i;
      }
      continue;
    }

    if (uniqueCount < maxEntries) {
      representatives[uniqueCount++] = i;
    }
  }

  return uniqueCount;
}

void printSSIDContinuation(const String& part) {
  Serial.printf("|     | %-22s | %-6s | %-2s | %-9s | %-17s |\r\n",
                part.c_str(), "", "", "", "");
}

void printScanGrid(const int* representatives, int uniqueCount) {
  // Every row has exactly the same 78-column geometry. Long SSIDs wrap only
  // inside the SSID field.
  Serial.println();
  printGridSeparator();
  Serial.println("| #   | SSID                   | RSSI   | CH | SECURITY  | BSSID             |");
  printGridSeparator();

  for (int i = 0; i < uniqueCount; ++i) {
    const int index = representatives[i];
    const String ssid = WiFi.SSID(index);
    const String security = authModeName(WiFi.encryptionType(index));
    const String bssid = WiFi.BSSIDstr(index);

    const size_t lineCount =
        max<size_t>(1, (ssid.length() + SSID_WIDTH - 1) / SSID_WIDTH);

    for (size_t line = 0; line < lineCount; ++line) {
      const size_t offset = line * SSID_WIDTH;
      const String part =
          ssid.substring(offset, min(offset + SSID_WIDTH, ssid.length()));

      if (line == 0) {
        Serial.printf("| %-3d | %-22s | %-6d | %-2d | %-9s | %-17s |\r\n",
                      i + 1,
                      part.c_str(),
                      WiFi.RSSI(index),
                      WiFi.channel(index),
                      security.c_str(),
                      bssid.c_str());
      } else {
        printSSIDContinuation(part);
      }
    }
  }

  printGridSeparator();
}

void scan() {
  Serial.println();
  Serial.println("WiFi scan");
  Serial.println("---------");
  Serial.println("Scanning nearby networks...");

  // WiFi setup through scanning always uses DHCP.
  NetConfig::configureDHCP();

  // Always start a fresh scan from a clean STA state. Put the interface
  // into STA mode BEFORE calling disconnect(): after a failed connection we
  // may have deliberately powered the WiFi radio down with disconnect(true).
  // Calling disconnect() while WiFi is already uninitialized produces
  // ESP_ERR_WIFI_NOT_INIT (0x3001), even though a subsequent scan can still
  // recover. Avoid that noisy/invalid call sequence.
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.disconnect(false, false);
  delay(100);
  int count = WiFi.scanNetworks();

  if (count < 0) {
    Serial.println("WiFi scan failed.");
    WiFi.scanDelete();

    // Keep the initialized STA driver alive. A scan failure is not an
    // NVS reset condition, and turning WiFi off here only adds another
    // asynchronous state transition.
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.setAutoReconnect(false);
    return;
  }

  if (count == 0) {
    Serial.println("No networks found.");
    WiFi.scanDelete();
    return;
  }

  int representatives[MAX_UNIQUE_SSIDS];
  const int uniqueCount =
      buildUniqueSSIDList(count, representatives, MAX_UNIQUE_SSIDS);

  printScanGrid(representatives, uniqueCount);

  Serial.println();
  Serial.print("Found ");
  Serial.print(count);
  Serial.print(" access points across ");
  Serial.print(uniqueCount);
  Serial.println(" unique SSIDs.");
  Serial.println("B. Back");

  String choice = Console::readLine(false);
  if (Console::disconnected()) return;
  choice.trim();
  choice.toUpperCase();

  if (choice == "B") {
    WiFi.scanDelete();
    return;
  }

  bool numeric = choice.length() > 0;
  for (size_t i = 0; i < choice.length(); ++i) {
    if (!isDigit(choice[i])) {
      numeric = false;
      break;
    }
  }

  int selected = numeric ? choice.toInt() : 0;

  if (selected < 1 || selected > uniqueCount) {
    Serial.println("Invalid network selection.");
    WiFi.scanDelete();
    return;
  }

  const int index = representatives[selected - 1];
  String ssid = WiFi.SSID(index);
  wifi_auth_mode_t encryption = WiFi.encryptionType(index);

  TargetAP selectedTarget;
  selectedTarget.valid = true;
  selectedTarget.channel = WiFi.channel(index);
  selectedTarget.rssi = WiFi.RSSI(index);
  selectedTarget.auth = encryption;
  const uint8_t* selectedBSSID = WiFi.BSSID(index);
  if (selectedBSSID == nullptr) {
    selectedTarget.valid = false;
  } else {
    memcpy(selectedTarget.bssid, selectedBSSID, 6);
  }

  Serial.println();
  Serial.print("Selected SSID: ");
  Serial.println(ssid);

  Serial.print("BSSID: ");
  Serial.println(WiFi.BSSIDstr(index));
  Serial.print("Channel: ");
  Serial.println(WiFi.channel(index));
  Serial.print("Security: ");
  Serial.println(authModeName(encryption));

  // The scan is SSID-deduplicated, so show every BSS advertising the
  // selected SSID. This lets us see whether the mesh APs agree on security
  // and which AP was chosen as the strongest representative.
  Serial.println("Matching access points:");
  for (int i = 0; i < WiFi.scanComplete(); ++i) {
    if (WiFi.SSID(i) == ssid) {
      Serial.print("  ");
      Serial.print(WiFi.BSSIDstr(i));
      Serial.print("  CH ");
      Serial.print(WiFi.channel(i));
      Serial.print("  RSSI ");
      Serial.print(WiFi.RSSI(i));
      Serial.print("  ");
      Serial.println(authModeName(WiFi.encryptionType(i)));
    }
  }

  if (encryption == WIFI_AUTH_OPEN) {
    Serial.println("No password required.");
  } else {
    Serial.println("Security: password required.");
    String password = Console::readPassword("PASSWORD: ");
    if (Console::disconnected()) {
      WiFi.scanDelete();
      return;
    }

    WiFi.scanDelete();

    // Selecting a network from a scan always uses DHCP for the connection.
    NetConfig::configureDHCP();

    Serial.println();
    Serial.println("Testing credentials...");
    Serial.println("Credentials will be saved only if the connection succeeds.");

    if (connectWithCredentials(ssid, password, &selectedTarget) == ConnectResult::SUCCESS) {
      Serial.println();
      Serial.println("================================");
      Serial.println("WiFi connection SUCCESSFUL");
      Serial.println("================================");
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.println();
    } else {
      Serial.println();
      Serial.println("================================");
      Serial.println("WiFi connection FAILED");
      Serial.println("================================");
      Serial.println("Credentials were NOT saved.");
      Serial.println("Previously saved credentials were left unchanged.");
      Serial.println();
    }
    return;
  }

  // Keep the selected AP details visible through the credential prompt.
  // The selected BSSID is used for this connection attempt only. Saved
  // credentials do not permanently bind the profile to that AP.
  
  // Selecting a network from a scan always uses DHCP for the connection.
  WiFi.scanDelete();
  NetConfig::configureDHCP();

  Serial.println();
  Serial.println("Testing open-network credentials...");
  Serial.println("Credentials will be saved only if the connection succeeds.");

  if (connectWithCredentials(ssid, "", &selectedTarget) == ConnectResult::SUCCESS) {
    Serial.println();
    Serial.println("================================");
    Serial.println("WiFi connection SUCCESSFUL");
    Serial.println("================================");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
  } else {
    Serial.println();
    Serial.println("================================");
    Serial.println("WiFi connection FAILED");
    Serial.println("================================");
    Serial.println("Credentials were NOT saved.");
    Serial.println("Previously saved credentials were left unchanged.");
    Serial.println();
  }
}

void setupCredentials() {
  Serial.println();
  Serial.println("WiFi configuration");
  Serial.println("------------------");

  Serial.println("Use the scan first if you want to see nearby networks.");
  Serial.println();

  String ssid = Console::readPrompt("SSID: ");
  if (Console::disconnected()) return;
  String password = Console::readPassword("PASSWORD: ");
  if (Console::disconnected()) return;

  // Entering SSID/password always starts from DHCP.
  NetConfig::configureDHCP();

  if (ssid.isEmpty()) {
    Serial.println("WiFi: SSID cannot be empty. Nothing was changed.");
    return;
  }

  Serial.println();
  Serial.println("Testing credentials...");
  Serial.println("Credentials will be saved only if the connection succeeds.");

  if (connectWithCredentials(ssid, password) == ConnectResult::SUCCESS) {
    Serial.println("WiFi credentials saved after successful connection.");
  } else {
    Serial.println("WiFi credentials were NOT saved.");
    Serial.println("Previously saved credentials were left unchanged.");
  }
}

void menu() {
  while (true) {
    Serial.println();
    if (Console::ansiSupported) {
      Console::color("1;33m");
      Serial.println("+---------------------------+");
      Serial.println("|         WIFI SETUP        |");
      Serial.println("+---------------------------+");
      Console::resetStyle();
      Console::color("1;36m");
    } else {
      Serial.println("WIFI SETUP");
      Serial.println("==========");
    }
    Serial.println("1. Scan nearby networks");
    Serial.println("2. Configure SSID/password");
    Serial.println("3. Show WiFi status");
    Serial.println("4. Connect now");
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readMenuChoice("Select: ", "1234B");
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      scan();
    } else if (choice == "2") {
      setupCredentials();
    } else if (choice == "3") {
      printStatus();
    } else if (choice == "4") {
      connect();
    } else if (choice == "B") {
      return;
    }
  }
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);

  Serial.println();
  Serial.println("WiFi subsystem starting...");

  /*
   * Versioned baseline:
   *   Arduino-ESP32 3.3.12
   *   ESP-IDF 5.5.5
   *
   * persistent(false) is deliberately set BEFORE WiFi.mode(). Arduino-ESP32
   * applies that setting during low-level Wi-Fi initialization and selects
   * WIFI_STORAGE_RAM for the ESP-IDF driver configuration. Application
   * credentials remain in this module's own Preferences namespace.
   */
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(false);

  // Register before WiFi.mode() so the STA_START event is observable.
  WiFi.onEvent(onWiFiEvent);

  configureVerboseWiFiLogging();
  printDriverVersion();

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("WARNING: WiFi station mode failed to start.");
    return;
  }

  Serial.println("WiFi driver configuration storage: RAM (NVS writes disabled).");

  connect();
}
}