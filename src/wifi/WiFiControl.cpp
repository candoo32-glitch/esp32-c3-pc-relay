#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_event.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "WiFiControl.h"
#include "../interface/Console.h"
#include "../network/NetConfig.h"

namespace WiFiControl {
Preferences preferences;
static bool diagnosticsEnabled = true;

const char* authModeName(wifi_auth_mode_t authMode);

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

// IDF 6 exposes the actual station failure through WIFI_EVENT_STA_DISCONNECTED.
// Arduino's wl_status_t alone is not enough to diagnose an association failure.
static WiFiEventId_t wifiEventId = 0;
static QueueHandle_t wifiDiagnosticQueue = nullptr;

struct WiFiDiagnosticRecord {
  uint32_t event = 0;
  uint8_t reason = 0;
  int8_t rssi = -128;
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 0;
  uint8_t authmode = WIFI_AUTH_OPEN;
  uint32_t ip = 0;
  uint32_t gateway = 0;
  uint32_t netmask = 0;
};

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
  xQueueSend(wifiDiagnosticQueue, &record, 0);
}

void serviceDiagnostics() {
  if (!diagnosticsEnabled || wifiDiagnosticQueue == nullptr) {
    return;
  }

  // Diagnostics are rendered only by the main task, so the individual ANSI
  // sections below cannot interleave with the Wi-Fi event callback. Plain-text
  // mode preserves the exact same readable records without escape sequences.
  auto diagnosticColor = [](const char* code) {
    if (Console::ansiSupported) {
      Console::color(code);
    }
  };

  auto diagnosticReset = []() {
    if (Console::ansiSupported) {
      Console::resetStyle();
    }
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
      case ARDUINO_EVENT_WIFI_STA_START:
        printPrefix();
        printSection("1;33m", "START");
        Serial.print("\r\n");
        break;

      case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
        const char* auth =
            authModeName(static_cast<wifi_auth_mode_t>(record.authmode));

        printPrefix();
        printSection("1;32m", "CONNECTED");
        Serial.print(" | CH=");
        printSection("1;36m", String(record.channel).c_str());
        Serial.print(" | AUTH=");
        printSection("1;33m", auth);
        Serial.print(" | BSSID=");
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 record.bssid[0], record.bssid[1], record.bssid[2],
                 record.bssid[3], record.bssid[4], record.bssid[5]);
        printSection("1;35m", bssid);
        Serial.print("\r\n");
        break;
      }

      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const char* reason = wifiDisconnectReasonName(record.reason);

        printPrefix();
        printSection("1;31m", "DISCONNECTED");
        Serial.print(" | R=");
        char reasonNumber[4];
        snprintf(reasonNumber, sizeof(reasonNumber), "%u", record.reason);
        printSection("1;33m", reasonNumber);
        Serial.print(" ");
        printSection("1;31m", reason);
        Serial.print(" | RSSI=");
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", record.rssi);
        printSection("1;35m", rssi);
        Serial.print(" | BSSID=");
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
                 record.bssid[0], record.bssid[1], record.bssid[2],
                 record.bssid[3], record.bssid[4], record.bssid[5]);
        printSection("1;34m", bssid);
        Serial.print("\r\n");
        break;
      }

      case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        const String ip = IPAddress(record.ip).toString();
        const String gateway = IPAddress(record.gateway).toString();
        const String netmask = IPAddress(record.netmask).toString();

        printPrefix();
        printSection("1;32m", "GOT_IP");
        Serial.print(" | IP=");
        printSection("1;32m", ip.c_str());
        Serial.print(" | GW=");
        printSection("1;36m", gateway.c_str());
        Serial.print(" | MASK=");
        printSection("1;33m", netmask.c_str());
        Serial.print("\r\n");
        break;
      }

      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        printPrefix();
        printSection("1;31m", "LOST_IP");
        Serial.print("\r\n");
        break;

      default:
        break;
    }
  }
}

void printFailureWordLine(const char* prefix, const char* word, const char* suffix = "") {
  Serial.print(prefix);
  if (Console::ansiSupported) Console::color("1;31m");
  Serial.print(word);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.println(suffix);
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

constexpr char PREF_NAMESPACE[] = "wifi";
constexpr char SSID_KEY[] = "ssid";
constexpr char PASSWORD_KEY[] = "password";
constexpr char HOSTNAME[] = "esp32-c3-relay";
constexpr char DIAGNOSTICS_KEY[] = "diagnostics";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

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
  CONNECTION_FAILED,
  TIMEOUT,
  NETWORK_CONFIG_FAILED,
  SERIAL_DISCONNECTED
};

const char* connectResultName(ConnectResult result) {
  switch (result) {
    case ConnectResult::SUCCESS: return "SUCCESS";
    case ConnectResult::SSID_NOT_FOUND: return "SSID_NOT_FOUND";
    case ConnectResult::CONNECTION_FAILED: return "CONNECTION_FAILED";
    case ConnectResult::TIMEOUT: return "TIMEOUT";
    case ConnectResult::NETWORK_CONFIG_FAILED: return "NETWORK_CONFIG_FAILED";
    case ConnectResult::SERIAL_DISCONNECTED: return "SERIAL_DISCONNECTED";
    default: return "UNKNOWN";
  }
}

ConnectResult connectWithCredentials(const String& ssid,
                                      const String& password,
                                      const TargetAP* requestedTarget = nullptr) {
  if (ssid.isEmpty()) {
    Serial.println("WiFi: not configured.");
    return ConnectResult::SSID_NOT_FOUND;
  }

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  /*
   * Use the standard Arduino-ESP32 station connection path.
   *
   * In particular, do NOT pin a scanned connection to a BSSID/channel here.
   * The ESP32-C3 may be behind a mesh, multiple APs, or an AP that changes
   * its association path. The documented WiFi.begin(ssid, password) path
   * lets the IDF station driver perform its normal AP selection and roaming.
   *
   * No raw wifi_config_t is constructed or modified here.
   * No esp_wifi_* connection calls are required.
   */
  wl_status_t status;

  // Connection baseline:
  //   * do not pin a BSSID
  //   * do not force a channel
  //   * scan all channels
  //   * disable automatic reconnect during this transaction
  //
  // The previous implementation locked the scan-selected BSSID/channel.
  // The hardware is repeatedly reporting AUTH_EXPIRE against that BSSID.
  // Espressif defines AUTH_EXPIRE as an authentication timeout or a reason
  // received from the AP. Removing the pin lets the station driver perform
  // its normal AP selection and gives us a clean test of the standard path.
  const bool previousAutoReconnect = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Arduino-ESP32 4.0 defaults the minimum STA security to WPA2-PSK.
  // An OPEN AP must explicitly lower that threshold before WiFi.begin().
  const wifi_auth_mode_t minimumSecurity =
      password.isEmpty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  WiFi.setMinSecurity(minimumSecurity);
  Serial.print("Minimum WiFi security: ");
  Serial.println(authModeName(minimumSecurity));

  if (!NetConfig::apply()) {
    printFailureWordLine("ERROR: network/IP configuration ", "failed", ".");
    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }

  if (requestedTarget != nullptr && requestedTarget->valid) {
    Serial.printf(
        "Diagnostic scan target: BSSID %02X:%02X:%02X:%02X:%02X:%02X CH %ld RSSI %d dBm\r\n",
        requestedTarget->bssid[0], requestedTarget->bssid[1],
        requestedTarget->bssid[2], requestedTarget->bssid[3],
        requestedTarget->bssid[4], requestedTarget->bssid[5],
        static_cast<long>(requestedTarget->channel),
        static_cast<int>(requestedTarget->rssi));
    Serial.println("Connection mode: SSID-based; BSSID/channel pinning DISABLED.");
  }

  status = WiFi.begin(
      ssid.c_str(),
      password.isEmpty() ? nullptr : password.c_str());

  Serial.print("WiFi.begin() returned status: ");


  if (status == WL_CONNECT_FAILED) {
    printFailureWordLine("WiFi.begin() ", "failed", ".");
    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::CONNECTION_FAILED;
  }

  const uint32_t startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < CONNECT_TIMEOUT_MS) {
    serviceDiagnostics();
    if (Console::disconnected()) {
      WiFi.disconnect();
      WiFi.setAutoReconnect(previousAutoReconnect);
      Console::prepareForMenuInput();
      return ConnectResult::SERIAL_DISCONNECTED;
    }
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection timed out.");
    Serial.print("Final WiFi status: ");
    Serial.println(static_cast<int>(WiFi.status()));
    WiFi.disconnect(false, false);
    delay(100);
    serviceDiagnostics();
    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::TIMEOUT;
  }

  serviceDiagnostics();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  preferences.putString(SSID_KEY, ssid);
  preferences.putString(PASSWORD_KEY, password);

  WiFi.setAutoReconnect(previousAutoReconnect);
  Console::prepareForMenuInput();
  return ConnectResult::SUCCESS;
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
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.println("+-----+------------------------+--------+----+-----------+-------------------+");
  if (Console::ansiSupported) Console::resetStyle();
}

void printScanHeader() {
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.println("| #   | SSID                   | RSSI   | CH | SECURITY  | BSSID             |");
  if (Console::ansiSupported) Console::resetStyle();
}

void printScanRow(int number, const String& ssid, int rssi, int channel,
                  const String& security, const String& bssid,
                  bool continuation) {
  if (!Console::ansiSupported) {
    if (continuation) {
      Serial.printf("|     | %-22s | %-6s | %-2s | %-9s | %-17s |\r\n",
                    ssid.c_str(), "", "", "", "");
    } else {
      Serial.printf("| %-3d | %-22s | %-6d | %-2d | %-9s | %-17s |\r\n",
                    number, ssid.c_str(), rssi, channel,
                    security.c_str(), bssid.c_str());
    }
    return;
  }

  Serial.print("| ");
  Console::color("1;33m");
  if (continuation) Serial.print("   ");
  else Serial.printf("%-3d", number);
  Console::resetStyle();
  Serial.print(" | ");

  Console::color("1;37m"); Serial.printf("%-22s", ssid.c_str()); Console::resetStyle();
  Serial.print(" | ");

  Console::color(rssi >= -50 ? "1;32m" : (rssi >= -70 ? "1;33m" : "1;31m"));
  Serial.printf("%-6d", rssi); Console::resetStyle();
  Serial.print(" | ");

  Console::color("1;36m"); Serial.printf("%-2d", channel); Console::resetStyle();
  Serial.print(" | ");

  Console::color(security == "OPEN" ? "1;32m" : "1;35m");
  Serial.printf("%-9s", security.c_str()); Console::resetStyle();
  Serial.print(" | ");

  Console::color("1;34m"); Serial.printf("%-17s", bssid.c_str()); Console::resetStyle();
  Serial.print(" |\r\n");
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

      printScanRow(i + 1, part, WiFi.RSSI(index), WiFi.channel(index),
                    security, bssid, line != 0);
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
    printFailureWordLine("WiFi scan ", "failed", ".");
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
      printFailureWordLine("WiFi connection ", "FAILED");
      Serial.println("================================");
      Serial.println("Credentials were NOT saved.");
      Serial.println("Previously saved credentials were left unchanged.");
      Serial.println();
    }
    return;
  }

  // Keep the selected AP details visible through the credential prompt.
  // The connection itself uses the standard SSID-based station path.

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

void printDiagnosticsSetting() {
  if (Console::ansiSupported) {
    Console::color(diagnosticsEnabled ? "1;32m" : "1;31m");
  }
  Serial.print("5. Diagnostics: ");
  Serial.println(diagnosticsEnabled ? "ON" : "OFF");
  if (Console::ansiSupported) {
    Console::resetStyle();
  }
}

void toggleDiagnostics() {
  diagnosticsEnabled = !diagnosticsEnabled;
  preferences.putBool(DIAGNOSTICS_KEY, diagnosticsEnabled);

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

void menu() {
  while (true) {
    serviceDiagnostics();
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
    printDiagnosticsSetting();
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readMenuChoice("Select: ", "12345B");
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
    } else if (choice == "5") {
      toggleDiagnostics();
    } else if (choice == "B") {
      return;
    }
  }
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);
  diagnosticsEnabled = preferences.getBool(DIAGNOSTICS_KEY, true);

  wifiDiagnosticQueue = xQueueCreate(32, sizeof(WiFiDiagnosticRecord));
  if (wifiDiagnosticQueue == nullptr) {
    Serial.println("WARNING: WiFi diagnostic queue allocation failed.");
  }

  // Use Arduino-ESP32's public WiFi event API so diagnostics follow the
  // same event translation used internally by the STA implementation.
  // One callback for all Wi-Fi events. The handler switches on the
  // event ID, avoiding multiple asynchronous callback registrations.
  wifiEventId = WiFi.onEvent(wifiArduinoEventHandler);

  Serial.println();
  Serial.println("WiFi subsystem starting...");

  // Initialize the station configuration before starting the Wi-Fi interface.
  // This matches the ordering used by Espressif's Arduino ESP32 examples.
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);

  connect();
}
}