#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "WiFiControl.h"
#include "../interface/Console.h"
#include "../network/NetConfig.h"
#include "WiFiDiagnostics.h"

namespace WiFiControl {
Preferences preferences;
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

void printFailureWordLine(const char* prefix, const char* word, const char* suffix = "") {
  Serial.print(prefix);
  if (Console::ansiSupported) Console::color("1;31m");
  Serial.print(word);
  if (Console::ansiSupported) Console::resetStyle();
  Serial.println(suffix);
}

constexpr char PREF_NAMESPACE[] = "wifi";
constexpr char SSID_KEY[] = "ssid";
constexpr char PASSWORD_KEY[] = "password";
constexpr char TX_POWER_KEY[] = "tx_power";
constexpr char WIFI_ENABLED_KEY[] = "enabled";
constexpr bool DEFAULT_WIFI_ENABLED = true;
constexpr int8_t DEFAULT_TX_POWER_QUARTER_DBM = 60;  // 15 dBm; experimentally stable
constexpr char HOSTNAME[] = "esp32-c3-relay";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

volatile bool reconnectSuppressed = false;
volatile bool connectionAttemptActive = false;
volatile bool wifiEnabled = DEFAULT_WIFI_ENABLED;
TaskHandle_t reconnectTaskHandle = nullptr;

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

bool applyTxPower(int8_t requestedQuarterDbm, bool persist) {
  const int clamped = max(8, min(72, static_cast<int>(requestedQuarterDbm)));
  const esp_err_t result = esp_wifi_set_max_tx_power(static_cast<int8_t>(clamped));
  if (result != ESP_OK) {
    Serial.print("WiFi TX power: failed to set (");
    Serial.print(esp_err_to_name(result));
    Serial.println(").");
    return false;
  }

  int8_t applied = 0;
  const esp_err_t readResult = esp_wifi_get_max_tx_power(&applied);
  if (readResult != ESP_OK) {
    Serial.print("WiFi TX power: set succeeded, readback failed (");
    Serial.print(esp_err_to_name(readResult));
    Serial.println(").");
    return false;
  }

  if (persist) {
    const size_t saved = preferences.putUChar(TX_POWER_KEY, static_cast<uint8_t>(applied));
    if (saved != sizeof(uint8_t)) {
      Serial.println("WiFi TX power: WARNING - NVS save failed.");
      return false;
    }
  }

  Serial.print("WiFi TX power: ");
  Serial.print(static_cast<float>(applied) * 0.25f, 2);
  Serial.println(" dBm.");
  return true;
}

void configureTxPower() {
  uint8_t saved = preferences.getUChar(
      TX_POWER_KEY, static_cast<uint8_t>(DEFAULT_TX_POWER_QUARTER_DBM));

  // 20 dBm and other out-of-range legacy values are migrated to the
  // experimentally stable 15 dBm setting.
  if (saved < 8 || saved > 72) {
    saved = static_cast<uint8_t>(DEFAULT_TX_POWER_QUARTER_DBM);
    preferences.putUChar(TX_POWER_KEY, saved);
  } else if (!preferences.isKey(TX_POWER_KEY)) {
    // Make the factory default explicit in NVS so the boot value can be
    // inspected and verified rather than existing only as a code fallback.
    preferences.putUChar(TX_POWER_KEY, saved);
  }

  Serial.print("WiFi boot TX power request: ");
  Serial.print(static_cast<float>(saved) * 0.25f, 2);
  Serial.println(" dBm");

  if (applyTxPower(static_cast<int8_t>(saved), false)) {
    int8_t applied = 0;
    if (esp_wifi_get_max_tx_power(&applied) == ESP_OK) {
      Serial.print("WiFi boot TX power applied: ");
      Serial.print(static_cast<float>(applied) * 0.25f, 2);
      Serial.println(" dBm");
    }
    return;
  }

  // Fall back to the experimentally stable 15 dBm setting if application fails.
  applyTxPower(DEFAULT_TX_POWER_QUARTER_DBM, false);
}

void printTxPowerOption(const char* key, float dbm, const char* description,
                       bool selected) {
  if (Console::ansiSupported) {
    Console::color(selected ? "1;32m" : "1;36m");
  }
  Serial.print(key);
  Serial.print(". ");
  Serial.print(dbm, 2);
  Serial.print(" dBm");
  if (Console::ansiSupported) Console::resetStyle();
  Serial.print(" - ");
  if (Console::ansiSupported) Console::color("1;37m");
  Serial.println(description);
  if (Console::ansiSupported) Console::resetStyle();
}

void configureTxPowerMenu() {
  int8_t current = DEFAULT_TX_POWER_QUARTER_DBM;
  if (esp_wifi_get_max_tx_power(&current) != ESP_OK) {
    current = DEFAULT_TX_POWER_QUARTER_DBM;
  }

  Serial.println();
  if (Console::ansiSupported) Console::color("1;36m");
  Serial.println("+---------------------------------------------+");
  Serial.println("|              WIFI TX POWER                  |");
  Serial.println("+---------------------------------------------+");
  if (Console::ansiSupported) Console::resetStyle();

  Serial.print("Current: ");
  if (Console::ansiSupported) Console::color("1;32m");
  Serial.print(static_cast<float>(current) * 0.25f, 2);
  Serial.println(" dBm");
  if (Console::ansiSupported) Console::resetStyle();
  Serial.println();

  printTxPowerOption("1", 18.00f, "High performance", current == 72);
  printTxPowerOption("2", 15.00f, "Balanced", current == 60);
  printTxPowerOption("3", 11.00f, "Low power", current == 44);
  printTxPowerOption("4", 8.50f, "Recommended for power stability", current == 34);
  printTxPowerOption("5", 2.00f, "Minimum power / short range", current == 8);
  if (Console::ansiSupported) Console::color("1;35m");
  Serial.println("6. Manual entry - enter a custom value");
  if (Console::ansiSupported) Console::resetStyle();
  Serial.println("B. Back");
  Serial.println();

  String choice = Console::readMenuChoice("Select: ", "123456B");
  choice.trim();
  choice.toUpperCase();

  int8_t requestedQuarterDbm = 0;
  bool selectedPreset = true;

  if (choice == "1") requestedQuarterDbm = 72;
  else if (choice == "2") requestedQuarterDbm = 60;
  else if (choice == "3") requestedQuarterDbm = 44;
  else if (choice == "4") requestedQuarterDbm = 34;
  else if (choice == "5") requestedQuarterDbm = 8;
  else if (choice == "6") selectedPreset = false;
  else return;

  if (!selectedPreset) {
    Serial.println();
    Serial.println("Manual TX power: enter 2.00 to 18.00 dBm.");
    Serial.println("20.00 dBm is intentionally unavailable.");
    String entry = Console::readLine(false);
    if (Console::disconnected()) return;
    entry.trim();

    const float requestedDbm = entry.toFloat();
    if (requestedDbm < 2.0f || requestedDbm > 18.0f) {
      if (Console::ansiSupported) Console::color("1;31m");
      Serial.println("Invalid TX power. Enter 2.00 to 18.00 dBm.");
      if (Console::ansiSupported) Console::resetStyle();
      return;
    }

    requestedQuarterDbm = static_cast<int8_t>(lroundf(requestedDbm * 4.0f));
  }

  if (!applyTxPower(requestedQuarterDbm, true)) {
    if (Console::ansiSupported) Console::color("1;31m");
    Serial.println("TX power was not changed or saved.");
    if (Console::ansiSupported) Console::resetStyle();
  } else {
    if (Console::ansiSupported) Console::color("1;32m");
    Serial.println("TX power saved to NVS and will be restored on boot.");
    if (Console::ansiSupported) Console::resetStyle();
  }
}

bool isEnabled() {
  return wifiEnabled;
}

bool saveEnabledPreference(bool enabled) {
  const size_t saved = preferences.putBool(WIFI_ENABLED_KEY, enabled);
  if (saved != 1) {
    Serial.println("WiFi enable setting: WARNING - NVS save failed.");
    return false;
  }
  return true;
}

void applyEnabledState(bool enabled, bool connectIfEnabled) {
  wifiEnabled = enabled;
  WiFi.setAutoReconnect(false);
  reconnectSuppressed = !enabled;

  if (!enabled) {
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi: OFF.");
    return;
  }

  if (!WiFi.mode(WIFI_STA)) {
    Serial.println("WiFi: failed to start station mode.");
    return;
  }

  configureTxPower();
  WiFi.setAutoReconnect(true);
  reconnectSuppressed = false;

  if (connectIfEnabled) {
    connect();
  }
}

void configureWiFiEnabled() {
  wifiEnabled = preferences.getBool(WIFI_ENABLED_KEY, DEFAULT_WIFI_ENABLED);
  if (!preferences.isKey(WIFI_ENABLED_KEY)) {
    if (!saveEnabledPreference(DEFAULT_WIFI_ENABLED)) {
      wifiEnabled = DEFAULT_WIFI_ENABLED;
    }
  }

  Serial.print("WiFi boot state: ");
  Serial.println(wifiEnabled ? "ON" : "OFF");
}

void configureWiFiEnabledMenu() {
  const bool newState = !wifiEnabled;
  if (!saveEnabledPreference(newState)) {
    if (Console::ansiSupported) Console::color("1;31m");
    Serial.println("WiFi state was not changed because the NVS save failed.");
    if (Console::ansiSupported) Console::resetStyle();
    return;
  }

  applyEnabledState(newState, newState);
  if (Console::ansiSupported) Console::color(newState ? "1;32m" : "1;33m");
  Serial.print("WiFi is now ");
  Serial.print(newState ? "ON" : "OFF");
  Serial.println(" and this setting is saved to NVS.");
  if (Console::ansiSupported) Console::resetStyle();
}

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
  if (!wifiEnabled) {
    Serial.println("WiFi is OFF. Enable WiFi before connecting.");
    return ConnectResult::CONNECTION_FAILED;
  }

  struct ConnectionAttemptGuard {
    ConnectionAttemptGuard() { connectionAttemptActive = true; }
    ~ConnectionAttemptGuard() { connectionAttemptActive = false; }
  } connectionAttemptGuard;
  if (ssid.isEmpty()) {
    Serial.println("WiFi: not configured.");
    return ConnectResult::SSID_NOT_FOUND;
  }

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  /*
   * Use the public ESP-IDF station API directly for this diagnostic
   * connection attempt. Do not pin a scanned connection to a BSSID/channel;
   * let the IDF station driver perform normal AP selection and roaming.
   */
  // Connection baseline:
  //   * bypass Arduino's STA::connect() configuration wrapper
  //   * do not pin a BSSID
  //   * do not force a channel
  //   * scan all channels and sort by signal
  //   * disable modem sleep for this diagnostic attempt
  //
  // The Arduino WiFi.begin() path still produced AUTH_EXPIRE. Build the
  // station configuration explicitly with the public ESP-IDF API so this
  // attempt isolates the Arduino wrapper from the Wi-Fi driver.
  const bool previousAutoReconnect = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false);
  WiFiDiagnostics::beginConnectionAttempt();

  const wifi_auth_mode_t minimumSecurity =
      password.isEmpty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  WiFi.setMinSecurity(minimumSecurity);

  WiFiDiagnostics::printLine("MIN_SECURITY", authModeName(minimumSecurity),
                       "1;33m", "1;33m");
  const bool pinTarget = requestedTarget != nullptr && requestedTarget->valid;
  WiFiDiagnostics::printText("PATH=RAW ESP-IDF STATION CONFIGURATION.", "1;37m");
  WiFiDiagnostics::printText(
      pinTarget ? "BSSID_CHANNEL_PINNING=ENABLED FOR DIAGNOSTIC A/B TEST."
                : "BSSID_CHANNEL_PINNING=DISABLED.",
      "1;37m");
  WiFiDiagnostics::printText("PMF=OPTIONAL / NOT REQUIRED.", "1;37m");
  WiFiDiagnostics::printText("MODEM_SLEEP=DISABLED FOR DIAGNOSTIC ATTEMPT.", "1;37m");

  if (!NetConfig::apply()) {
    printFailureWordLine("ERROR: network/IP configuration ", "failed", ".");
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }

  if (requestedTarget != nullptr && requestedTarget->valid && WiFiDiagnostics::enabled()) {
    char target[96];
    snprintf(target, sizeof(target),
             "BSSID=%02X:%02X:%02X:%02X:%02X:%02X CH=%ld RSSI=%d dBm",
             requestedTarget->bssid[0], requestedTarget->bssid[1],
             requestedTarget->bssid[2], requestedTarget->bssid[3],
             requestedTarget->bssid[4], requestedTarget->bssid[5],
             static_cast<long>(requestedTarget->channel),
             static_cast<int>(requestedTarget->rssi));
    WiFiDiagnostics::printText(target, "1;35m");
  }

  wifi_config_t stationConfig = {};
  ssid.toCharArray(
      reinterpret_cast<char*>(stationConfig.sta.ssid),
      sizeof(stationConfig.sta.ssid));

  if (!password.isEmpty()) {
    password.toCharArray(
        reinterpret_cast<char*>(stationConfig.sta.password),
        sizeof(stationConfig.sta.password));
  }

  stationConfig.sta.channel = pinTarget ? static_cast<uint8_t>(requestedTarget->channel) : 0;
  stationConfig.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  stationConfig.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  stationConfig.sta.failure_retry_cnt = 3;
  stationConfig.sta.threshold.rssi = -127;
  stationConfig.sta.threshold.authmode = minimumSecurity;
  stationConfig.sta.pmf_cfg.capable = false;
  stationConfig.sta.pmf_cfg.required = false;
  stationConfig.sta.bssid_set = pinTarget ? 1 : 0;
  if (pinTarget) {
    memcpy(stationConfig.sta.bssid, requestedTarget->bssid,
           sizeof(stationConfig.sta.bssid));
  } else {
    memset(stationConfig.sta.bssid, 0, sizeof(stationConfig.sta.bssid));
  }

  const esp_err_t setConfigResult =
      esp_wifi_set_config(WIFI_IF_STA, &stationConfig);
  if (setConfigResult != ESP_OK) {
    WiFiDiagnostics::printFailure("SET_CONFIG", esp_err_to_name(setConfigResult));
    WiFi.setAutoReconnect(previousAutoReconnect);
    Console::prepareForMenuInput();
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }

  wifi_config_t committedConfig = {};
  const esp_err_t getConfigResult =
      esp_wifi_get_config(WIFI_IF_STA, &committedConfig);
  if (getConfigResult == ESP_OK) {
    char configSummary[128];
    snprintf(configSummary, sizeof(configSummary),
             "CH=%u BSSID_SET=%u AUTHMODE=%u RETRIES=%u PMF_CAPABLE=%u PMF_REQUIRED=%u",
             static_cast<unsigned>(committedConfig.sta.channel),
             static_cast<unsigned>(committedConfig.sta.bssid_set),
             static_cast<unsigned>(committedConfig.sta.threshold.authmode),
             static_cast<unsigned>(committedConfig.sta.failure_retry_cnt),
             static_cast<unsigned>(committedConfig.sta.pmf_cfg.capable),
             static_cast<unsigned>(committedConfig.sta.pmf_cfg.required));
    WiFiDiagnostics::printText(configSummary, "1;36m");
  } else {
    WiFiDiagnostics::printFailure("GET_CONFIG", esp_err_to_name(getConfigResult));
  }

  const esp_err_t powerSaveResult = esp_wifi_set_ps(WIFI_PS_NONE);
  if (powerSaveResult != ESP_OK) {
    WiFiDiagnostics::printFailure("POWER_SAVE_DISABLE", esp_err_to_name(powerSaveResult));
  }

  const esp_err_t connectResult = esp_wifi_connect();
  if (connectResult != ESP_OK) {
    WiFiDiagnostics::printFailure("CONNECT", esp_err_to_name(connectResult));
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::CONNECTION_FAILED;
  }

  WiFiDiagnostics::printLine("CONNECT_RETURN", esp_err_to_name(connectResult),
                       "1;36m", connectResult == ESP_OK ? "1;32m" : "1;31m");
  char initialStatus[16];
  snprintf(initialStatus, sizeof(initialStatus), "%d",
           static_cast<int>(WiFi.status()));
  WiFiDiagnostics::printLine("INITIAL_STATUS", initialStatus,
                       "1;36m", "1;33m");

  const uint32_t startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < CONNECT_TIMEOUT_MS) {
    WiFiDiagnostics::service();
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection timed out.");
    char finalStatus[16];
    snprintf(finalStatus, sizeof(finalStatus), "%d",
             static_cast<int>(WiFi.status()));
    WiFiDiagnostics::printLine("FINAL_STATUS", finalStatus,
                         "1;36m", "1;31m");
    WiFi.disconnect(false, false);
    delay(100);
    WiFiDiagnostics::service();
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::TIMEOUT;
  }

  WiFiDiagnostics::service();
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  const size_t savedSsid = preferences.putString(SSID_KEY, ssid);
  const size_t savedPassword = preferences.putString(PASSWORD_KEY, password);
  if (savedSsid == 0 || savedPassword == 0) {
    Serial.println("WARNING: WiFi credentials could not be fully saved to NVS.");
  } else {
    Serial.println("WiFi credentials saved to NVS.");
  }

  WiFi.setAutoReconnect(previousAutoReconnect);
  return ConnectResult::SUCCESS;
}

bool diagnosticsPreference() {
  return preferences.getBool("diagnostics", true);
}

bool saveDiagnosticsPreference(bool enabled) {
  return preferences.putBool("diagnostics", enabled) == 1;
}

void printStatus() {
  Serial.println();
  Serial.println("WiFi status");
  Serial.println("-----------");

  Serial.print("WiFi power: ");
  if (Console::ansiSupported) Console::color(wifiEnabled ? "1;32m" : "1;33m");
  Serial.println(wifiEnabled ? "ON" : "OFF");
  if (Console::ansiSupported) Console::resetStyle();

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
  if (!wifiEnabled) {
    Serial.println("WiFi is OFF. Enable WiFi before connecting.");
    return false;
  }

  const String ssid = preferences.getString(SSID_KEY, "");
  const String password = preferences.getString(PASSWORD_KEY, "");
  return connectWithCredentials(ssid, password) == ConnectResult::SUCCESS;
}

void reconnectTask(void*) {
  for (;;) {
    if (wifiEnabled && !reconnectSuppressed && !connectionAttemptActive &&
        WiFi.status() != WL_CONNECTED &&
        !preferences.getString(SSID_KEY, "").isEmpty()) {
      esp_wifi_set_ps(WIFI_PS_NONE);
      const esp_err_t result = esp_wifi_connect();
      if (result == ESP_OK) {
        WiFiDiagnostics::printText("AUTO_RECONNECT=REQUESTED", "1;33m");
      } else if (result != ESP_ERR_WIFI_CONN) {
        WiFiDiagnostics::printFailure("AUTO_RECONNECT", esp_err_to_name(result));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
  }
}

void service() {
  // Reconnection runs independently of the console/main loop. Keep this
  // service hook for diagnostics and future Wi-Fi maintenance.
  WiFiDiagnostics::service();
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
  if (!wifiEnabled) {
    Serial.println("WiFi is OFF. Enable WiFi before scanning.");
    return;
  }
  reconnectSuppressed = true;
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
    reconnectSuppressed = false;
    return;
  }

  if (count == 0) {
    Serial.println("No networks found.");
    WiFi.scanDelete();
    reconnectSuppressed = false;
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
    reconnectSuppressed = false;
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
    reconnectSuppressed = false;
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
      reconnectSuppressed = false;
      return;
    }

    WiFi.scanDelete();

    // Selecting a network from a scan always uses DHCP for the connection.
    NetConfig::configureDHCP();

    Serial.println();
    Serial.println("Testing credentials...");
    Serial.println("Credentials will be saved only if the connection succeeds.");

    if (connectWithCredentials(ssid, password) == ConnectResult::SUCCESS) {
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
    reconnectSuppressed = false;
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

  if (connectWithCredentials(ssid, "") == ConnectResult::SUCCESS) {
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
  reconnectSuppressed = false;
}

void setupCredentials() {
  if (!wifiEnabled) {
    Serial.println("WiFi is OFF. Enable WiFi before configuring credentials.");
    return;
  }
  reconnectSuppressed = true;
  Serial.println();
  Serial.println("WiFi configuration");
  Serial.println("------------------");

  Serial.println("Use the scan first if you want to see nearby networks.");
  Serial.println();

  String ssid = Console::readPrompt("SSID: ");
  if (Console::disconnected()) { reconnectSuppressed = false; return; }
  String password = Console::readPassword("PASSWORD: ");
  if (Console::disconnected()) { reconnectSuppressed = false; return; }

  // Entering SSID/password always starts from DHCP.
  NetConfig::configureDHCP();

  if (ssid.isEmpty()) {
    Serial.println("WiFi: SSID cannot be empty. Nothing was changed.");
    reconnectSuppressed = false;
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
  reconnectSuppressed = false;
}

void menu() {
  while (true) {
    WiFiDiagnostics::service();
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
    int8_t menuTxPower = DEFAULT_TX_POWER_QUARTER_DBM;
    if (esp_wifi_get_max_tx_power(&menuTxPower) != ESP_OK) {
      menuTxPower = DEFAULT_TX_POWER_QUARTER_DBM;
    }
    Serial.print("5. TX power: ");
    Serial.print(static_cast<float>(menuTxPower) * 0.25f, 2);
    Serial.println(" dBm");
    if (Console::ansiSupported) Console::color(wifiEnabled ? "1;32m" : "1;33m");
    Serial.print("6. WiFi: ");
    Serial.println(wifiEnabled ? "ON" : "OFF");
    if (Console::ansiSupported) Console::resetStyle();
    WiFiDiagnostics::printMenuSetting();
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readMenuChoice("Select: ", "1234567B");
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
      configureTxPowerMenu();
    } else if (choice == "6") {
      configureWiFiEnabledMenu();
    } else if (choice == "7") {
      WiFiDiagnostics::toggle();
    } else if (choice == "B") {
      return;
    }
  }
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);
  WiFiDiagnostics::begin();
  configureWiFiEnabled();

  Serial.println();
  Serial.println("WiFi subsystem starting...");

  // Initialize the station configuration before starting the Wi-Fi interface.
  // This matches the ordering used by Espressif's Arduino ESP32 examples.
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  if (!wifiEnabled) {
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi is disabled by the saved NVS setting.");
  } else {
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    configureTxPower();
    connect();
  }
  if (reconnectTaskHandle == nullptr) {
    xTaskCreate(
        reconnectTask,
        "wifi_reconnect",
        4096,
        nullptr,
        1,
        &reconnectTaskHandle);
  }
}
}