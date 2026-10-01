#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "esp_wifi.h"

#ifndef FW_BUILD_VERSION
#define FW_BUILD_VERSION 0
#endif

constexpr uint32_t FIRMWARE_BUILD_VERSION = FW_BUILD_VERSION;

namespace Pins {
constexpr uint8_t POWER_RELAY = 5; // S1
constexpr uint8_t RESET_RELAY = 6; // S2
}

namespace Relay {
constexpr uint8_t OFF = HIGH;
constexpr uint8_t ON  = LOW;

void begin() {
  // Relays are active-low. Set the output latch HIGH before switching the
  // GPIOs to OUTPUT so startup cannot intentionally drive a relay ON.
  digitalWrite(Pins::POWER_RELAY, OFF);
  digitalWrite(Pins::RESET_RELAY, OFF);
  pinMode(Pins::POWER_RELAY, OUTPUT);
  pinMode(Pins::RESET_RELAY, OUTPUT);
  digitalWrite(Pins::POWER_RELAY, OFF);
  digitalWrite(Pins::RESET_RELAY, OFF);
}
}

namespace Console {
bool ansiSupported = false;
volatile bool sessionLost = false;

// A CRLF is one Enter key even when CR and LF arrive in different USB
// packets. Keep this state until the next byte; do not use a short timeout.
bool consumePendingLineFeed = false;

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
void onHardwareCDCEvent(void* arg, esp_event_base_t eventBase,
                        int32_t eventId, void* eventData) {
  (void)arg;
  (void)eventData;
  if (eventBase == ARDUINO_HW_CDC_EVENTS &&
      eventId == ARDUINO_HW_CDC_BUS_RESET_EVENT) {
    // Let the main task reset the CDC transport safely.
    sessionLost = true;
  }
}
#endif

bool connected() {
  return !sessionLost && Serial.isConnected();
}

bool disconnected() {
  if (sessionLost || !Serial.isConnected()) {
    sessionLost = true;
    return true;
  }
  return false;
}

void resetTransport() {
  // Reinitialize native USB-Serial/JTAG after a detected disconnect/reset.
  // This clears stale RX/TX state before a new PuTTY session starts.
  Serial.end();
  delay(50);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(50);

  while (Serial.available()) {
    Serial.read();
  }

  consumePendingLineFeed = false;
  sessionLost = false;
}

void waitForConnection() {
  while (!Serial.isConnected()) {
    delay(50);
  }
  sessionLost = false;
}

void resetSession() {
  sessionLost = false;
  ansiSupported = false;
  consumePendingLineFeed = false;

  while (Serial.available()) {
    Serial.read();
  }
}

void detectANSI() {
  ansiSupported = false;
}

String readLine();

void begin() {
  resetSession();
  waitForConnection();

  Serial.println();
  Serial.println("Terminal display mode");
  Serial.println("---------------------");
  Serial.println("Use ANSI colors and boxed menus?");
  Serial.println("Y = ANSI");
  Serial.println("N = Plain text");
  Serial.print("Select [Y/N]: ");

  while (!Serial.available()) {
    delay(10);
  }

  String choice = readLine();
  choice.trim();
  choice.toUpperCase();

  ansiSupported = (choice == "Y" || choice == "YES");

  Serial.println();
  Serial.print("Terminal mode: ");
  Serial.println(ansiSupported ? "ANSI" : "plain text");
}

void ansi(const char* sequence) {
  if (ansiSupported) {
    Serial.write(0x1B);
    Serial.print("[");
    Serial.print(sequence);
  }
}

void resetStyle() {
  if (ansiSupported) {
    Serial.write(0x1B);
    Serial.print("[0m");
  }
}

void clearScreen() {
  if (!ansiSupported) return;
  Serial.write(0x1B); Serial.print("[2J");
  Serial.write(0x1B); Serial.print("[H");
}

void color(const char* code) {
  if (!ansiSupported) return;
  Serial.write(0x1B); Serial.print("["); Serial.print(code);
}

String readLine() {
  String value;
  bool inEscapeSequence = false;

  while (true) {
    if (disconnected()) {
      return "";
    }

    while (Serial.available()) {
      const uint8_t byte = static_cast<uint8_t>(Serial.read());
      const char c = static_cast<char>(byte);

      // CRLF is one Enter even when the USB CDC packet boundary splits the
      // pair. Consume only the matching LF; never treat it as a second command.
      if (consumePendingLineFeed) {
        consumePendingLineFeed = false;
        if (c == '\n') {
          continue;
        }
      }

      // Terminals can send cursor/function-key escape sequences. Do not let
      // their printable bytes ([, A, B, etc.) become menu commands.
      if (inEscapeSequence) {
        if ((byte >= 0x40 && byte <= 0x7E) || byte == 0x1B) {
          inEscapeSequence = (byte == 0x1B);
        }
        continue;
      }

      if (byte == 0x1B) {
        inEscapeSequence = true;
        continue;
      }

      if (c == '\r' || c == '\n') {
        consumePendingLineFeed = (c == '\r');
        Serial.println();
        return value;
      }

      if (c == '\b' || byte == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      // Never allow control traffic to become a command. This includes NUL,
      // CDC framing artifacts, and other non-printable bytes.
      if (byte < 0x20 || byte == 0x7F) {
        continue;
      }

      Serial.write(c);
      if (value.length() < 64) {
        value += c;
      }
    }

    delay(10);
  }
}

String readPrompt(const char* prompt) {
  Serial.print(prompt);
  return readLine();
}

bool yesNo(const char* prompt) {
  String value = readPrompt(prompt);
  value.trim();
  value.toUpperCase();
  return value == "Y" || value == "YES";
}

String readPassword(const char* prompt) {
  Serial.print(prompt);
  String value;
  bool inEscapeSequence = false;

  while (true) {
    if (disconnected()) {
      return "";
    }

    while (Serial.available()) {
      const uint8_t byte = static_cast<uint8_t>(Serial.read());
      const char c = static_cast<char>(byte);

      if (consumePendingLineFeed) {
        consumePendingLineFeed = false;
        if (c == '\n') continue;
      }

      // Password entry gets the same terminal escape filtering as menu input.
      // Cursor/function-key sequences must never become password characters.
      if (inEscapeSequence) {
        if ((byte >= 0x40 && byte <= 0x7E) || byte == 0x1B) {
          inEscapeSequence = (byte == 0x1B);
        }
        continue;
      }

      if (byte == 0x1B) {
        inEscapeSequence = true;
        continue;
      }

      if (c == '\r' || c == '\n') {
        consumePendingLineFeed = (c == '\r');
        Serial.println();
        return value;
      }

      if (c == '\b' || byte == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      if (byte < 0x20 || byte == 0x7F) {
        continue;
      }

      Serial.write('*');
      if (value.length() < 64) {
        value += c;
      }
    }

    delay(10);
  }
}
}

namespace NetConfig {
Preferences preferences;

constexpr char PREF_NAMESPACE[] = "network";
constexpr char MODE_KEY[] = "mode";
constexpr char IP_KEY[] = "ip";
constexpr char GATEWAY_KEY[] = "gateway";
constexpr char SUBNET_KEY[] = "subnet";
constexpr char DNS1_KEY[] = "dns1";
constexpr char DNS2_KEY[] = "dns2";

constexpr char HOSTNAME[] = "esp32-c3-relay";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

// Normal WiFi operation: let the ESP32-C3 select the AP/BSSID normally.
constexpr bool DIAGNOSTIC_PIN_BSSID = true;
// Let the ESP32 scan for this BSSID instead of assuming its channel.
constexpr uint8_t DIAGNOSTIC_CHANNEL = 0;
constexpr uint8_t DIAGNOSTIC_BSSID[6] = {
  0x86, 0xCC, 0x9C, 0x94, 0x4E, 0x18
};

enum class Mode : uint8_t {
  DHCP = 0,
  STATIC = 1
};

Mode mode() {
  return preferences.getUChar(MODE_KEY, static_cast<uint8_t>(Mode::DHCP))
           == static_cast<uint8_t>(Mode::STATIC)
         ? Mode::STATIC
         : Mode::DHCP;
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);
}

void printSettings() {
  Serial.println();
  Serial.println("Network settings");
  Serial.println("----------------");
  Serial.print("Mode: ");
  Serial.println(mode() == Mode::STATIC ? "MANUAL / STATIC" : "DHCP");

  if (mode() == Mode::STATIC) {
    Serial.print("IP:      ");
    Serial.println(preferences.getString(IP_KEY, "192.168.1.50"));
    Serial.print("Gateway: ");
    Serial.println(preferences.getString(GATEWAY_KEY, "192.168.1.1"));
    Serial.print("Subnet:  ");
    Serial.println(preferences.getString(SUBNET_KEY, "255.255.255.0"));
    Serial.print("DNS 1:   ");
    Serial.println(preferences.getString(DNS1_KEY, "192.168.1.1"));
    Serial.print("DNS 2:   ");
    Serial.println(preferences.getString(DNS2_KEY, "8.8.8.8"));
  }

  Serial.println();
}

bool parseIP(const String& text, IPAddress& address) {
  return address.fromString(text);
}

bool loadStatic(IPAddress& ip, IPAddress& gateway, IPAddress& subnet,
                IPAddress& dns1, IPAddress& dns2) {
  if (!parseIP(preferences.getString(IP_KEY, "192.168.1.50"), ip) ||
      !parseIP(preferences.getString(GATEWAY_KEY, "192.168.1.1"), gateway) ||
      !parseIP(preferences.getString(SUBNET_KEY, "255.255.255.0"), subnet) ||
      !parseIP(preferences.getString(DNS1_KEY, "192.168.1.1"), dns1) ||
      !parseIP(preferences.getString(DNS2_KEY, "8.8.8.8"), dns2)) {
    Serial.println("ERROR: one or more static network addresses are invalid.");
    return false;
  }

  return true;
}

void configureDHCP() {
  preferences.putUChar(MODE_KEY, static_cast<uint8_t>(Mode::DHCP));
  Serial.println("Network mode saved: DHCP.");
}

String currentIP() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return preferences.getString(IP_KEY, "192.168.1.50");
}

String currentGateway() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.gatewayIP().toString();
  }
  return preferences.getString(GATEWAY_KEY, "192.168.1.1");
}

String currentSubnet() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.subnetMask().toString();
  }
  return preferences.getString(SUBNET_KEY, "255.255.255.0");
}

String currentDNS1() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.dnsIP().toString();
  }
  return preferences.getString(DNS1_KEY, "192.168.1.1");
}

String currentDNS2() {
  return preferences.getString(DNS2_KEY, "8.8.8.8");
}

void configureStatic() {
  Serial.println();
  Serial.println("Manual / static IPv4 configuration");
  Serial.println("----------------------------------");
  Serial.println("Current network values are pre-filled. Press Enter to keep them.");
  Serial.println();

  String ip = Console::readPrompt(("IP address [" + currentIP() + "]: ").c_str());
  if (Console::disconnected()) return;
  String gateway = Console::readPrompt(("Gateway [" + currentGateway() + "]: ").c_str());
  if (Console::disconnected()) return;
  String subnet = Console::readPrompt(("Subnet mask [" + currentSubnet() + "]: ").c_str());
  if (Console::disconnected()) return;
  String dns1 = Console::readPrompt(("DNS 1 [" + currentDNS1() + "]: ").c_str());
  if (Console::disconnected()) return;
  String dns2 = Console::readPrompt(("DNS 2 [" + currentDNS2() + "]: ").c_str());
  if (Console::disconnected()) return;

  if (ip.isEmpty()) ip = currentIP();
  if (gateway.isEmpty()) gateway = currentGateway();
  if (subnet.isEmpty()) subnet = currentSubnet();
  if (dns1.isEmpty()) dns1 = currentDNS1();
  if (dns2.isEmpty()) dns2 = currentDNS2();

  IPAddress testIP;
  IPAddress testGateway;
  IPAddress testSubnet;
  IPAddress testDNS1;
  IPAddress testDNS2;

  if (!parseIP(ip, testIP) ||
      !parseIP(gateway, testGateway) ||
      !parseIP(subnet, testSubnet) ||
      !parseIP(dns1, testDNS1) ||
      !parseIP(dns2, testDNS2)) {
    Serial.println();
    Serial.println("ERROR: invalid IPv4 address. Nothing was changed.");
    return;
  }

  preferences.putString(IP_KEY, ip);
  preferences.putString(GATEWAY_KEY, gateway);
  preferences.putString(SUBNET_KEY, subnet);
  preferences.putString(DNS1_KEY, dns1);
  preferences.putString(DNS2_KEY, dns2);
  preferences.putUChar(MODE_KEY, static_cast<uint8_t>(Mode::STATIC));

  Serial.println();
  Serial.println("Manual network settings saved.");
}

void menu() {
  while (true) {
    Serial.println();
    if (Console::ansiSupported) {
      Console::color("1;33m");
      Serial.println("+---------------------------+");
      Serial.println("|       NETWORK SETUP       |");
      Serial.println("+---------------------------+");
      Console::resetStyle();
      Console::color("1;36m");
    } else {
      Serial.println("NETWORK SETUP");
      Serial.println("=============");
    }
    Serial.println("1. DHCP");
    Serial.println("2. Manual / Static IPv4");
    Serial.println("3. Show current settings");
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readPrompt("Select: ");
    if (Console::disconnected()) return;
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      configureDHCP();
    } else if (choice == "2") {
      configureStatic();
    } else if (choice == "3") {
      printSettings();
    } else if (choice == "B") {
      return;
    } else {
      Serial.println("Unknown selection.");
    }
  }
}

bool apply() {
  WiFi.setHostname(HOSTNAME);

  if (mode() == Mode::STATIC) {
    IPAddress ip, gateway, subnet, dns1, dns2;

    if (!loadStatic(ip, gateway, subnet, dns1, dns2)) {
      return false;
    }

    Serial.println("Network: using manual/static IPv4.");
    return WiFi.config(ip, gateway, subnet, dns1, dns2);
  }

  Serial.println("Network: using DHCP.");
  return true;
}
}

namespace WiFiControl {
Preferences preferences;

volatile uint8_t lastDisconnectReason = 0;
volatile int8_t lastDisconnectRSSI = 0;
volatile uint8_t disconnectEventCount = 0;
volatile uint8_t lastDisconnectBSSID[6] = {0, 0, 0, 0, 0, 0};

struct WiFiEventTraceEntry {
  uint32_t elapsedMs;
  arduino_event_id_t eventId;
  uint8_t reason;
};

constexpr uint8_t WIFI_EVENT_TRACE_MAX = 24;
volatile uint8_t wifiEventTraceCount = 0;
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
  wifiEventTraceCount = 0;
  memset((void*)wifiEventTrace, 0, sizeof(wifiEventTrace));
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  const arduino_event_id_t eventId =
      static_cast<arduino_event_id_t>(event);

  if (wifiEventTraceCount < WIFI_EVENT_TRACE_MAX) {
    const uint8_t index = wifiEventTraceCount++;
    wifiEventTrace[index].elapsedMs = millis();
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

// A/B baseline: keep the station path identical to the diagnostic build, but
// disable promiscuous capture so we can determine whether the sniffer itself
// interferes with association. Re-enable only after this baseline is tested.
constexpr bool WIFI_DIAGNOSTICS = false;

// Capture the actual 802.11 management-frame exchange during a diagnostic
// connection attempt. ESP-IDF exposes management frames through promiscuous
// mode even while the station is in STA mode. The callback runs in the Wi-Fi
// driver task, so it only copies small records into a ring buffer; all Serial
// output happens later from the application task.
namespace WiFiFrameCapture {
constexpr size_t MAX_ENTRIES = 64;

struct Entry {
  uint32_t timestampMs;
  int8_t rssi;
  uint8_t channel;
  uint8_t subtype;
  uint16_t length;
  uint16_t sequence;
  uint16_t code;
  uint8_t source[6];
  uint8_t destination[6];
  uint8_t bssid[6];
};

Entry entries[MAX_ENTRIES];
volatile size_t count = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t targetBSSID[6] = {0};
uint8_t stationMAC[6] = {0};
bool enabled = false;

uint16_t littleEndian16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}

bool addressEquals(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

const char* subtypeName(uint8_t subtype) {
  switch (subtype) {
    case 0: return "ASSOC_REQ";
    case 1: return "ASSOC_RESP";
    case 2: return "REASSOC_REQ";
    case 3: return "REASSOC_RESP";
    case 4: return "PROBE_REQ";
    case 5: return "PROBE_RESP";
    case 8: return "BEACON";
    case 10: return "DISASSOC";
    case 11: return "AUTH";
    case 12: return "DEAUTH";
    case 13: return "ACTION";
    default: return "MGMT";
  }
}

void captureCallback(void* buffer, wifi_promiscuous_pkt_type_t type) {
  if (!enabled || buffer == nullptr || type != WIFI_PKT_MGMT) {
    return;
  }

  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buffer);

  const uint16_t length = packet->rx_ctrl.sig_len;
  if (length < 24) {
    return;
  }

  const uint8_t* frame = packet->payload;
  const uint16_t frameControl = littleEndian16(frame);
  const uint8_t frameType = static_cast<uint8_t>((frameControl >> 2) & 0x03);
  const uint8_t subtype = static_cast<uint8_t>((frameControl >> 4) & 0x0F);

  if (frameType != 0) {
    return;
  }

  // Only retain frames that can tell us something about the association
  // exchange. We intentionally do not buffer the continuous beacon stream.
  if (subtype != 0 && subtype != 1 &&
      subtype != 2 && subtype != 3 &&
      subtype != 5 && subtype != 10 &&
      subtype != 11 && subtype != 12 &&
      subtype != 13) {
    return;
  }

  const uint8_t* destination = frame + 4;
  const uint8_t* source = frame + 10;
  const uint8_t* bssid = frame + 16;

  // For infrastructure management frames the AP can appear in any of these
  // address positions depending on the frame direction. Matching all three
  // makes the diagnostic robust to the particular management subtype.
  const bool relevant =
      addressEquals(destination, targetBSSID) ||
      addressEquals(source, targetBSSID) ||
      addressEquals(bssid, targetBSSID) ||
      addressEquals(destination, stationMAC) ||
      addressEquals(source, stationMAC) ||
      addressEquals(bssid, stationMAC);

  if (!relevant) {
    return;
  }

  Entry entry = {};
  entry.timestampMs = millis();
  entry.rssi = packet->rx_ctrl.rssi;
  entry.channel = packet->rx_ctrl.channel;
  entry.subtype = subtype;
  entry.length = length;
  entry.sequence =
      static_cast<uint16_t>(littleEndian16(frame + 22) >> 4);
  entry.code = 0xFFFF;
  memcpy(entry.source, source, 6);
  memcpy(entry.destination, destination, 6);
  memcpy(entry.bssid, bssid, 6);

  // Management header is 24 bytes. Decode the fixed fields that identify
  // exactly why an AP accepted or rejected the request.
  if (subtype == 11 && length >= 30) {
    // Authentication: algorithm, transaction sequence, status code.
    entry.sequence = littleEndian16(frame + 26);
    entry.code = littleEndian16(frame + 28);
  } else if ((subtype == 1 || subtype == 3) && length >= 30) {
    // Association/Reassociation response: capability, status, AID.
    entry.code = littleEndian16(frame + 26);
  } else if ((subtype == 10 || subtype == 12) && length >= 26) {
    // Disassociation/Deauthentication: reason code.
    entry.code = littleEndian16(frame + 24);
  }

  portENTER_CRITICAL_ISR(&mux);
  if (count < MAX_ENTRIES) {
    entries[count++] = entry;
  } else {
    for (size_t i = 1; i < MAX_ENTRIES; ++i) {
      entries[i - 1] = entries[i];
    }
    entries[MAX_ENTRIES - 1] = entry;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

bool begin(const uint8_t target[6]) {
  memcpy(targetBSSID, target, 6);

  esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, stationMAC);
  if (err != ESP_OK) {
    Serial.print("      802.11 capture: failed to read STA MAC: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }

  count = 0;
  enabled = true;

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;

  err = esp_wifi_set_promiscuous_rx_cb(captureCallback);
  if (err != ESP_OK) {
    enabled = false;
    Serial.print("      802.11 capture: callback setup failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }

  err = esp_wifi_set_promiscuous_filter(&filter);
  if (err != ESP_OK) {
    enabled = false;
    Serial.print("      802.11 capture: filter setup failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }

  err = esp_wifi_set_promiscuous(true);
  if (err != ESP_OK) {
    enabled = false;
    Serial.print("      802.11 capture: enable failed: ");
    Serial.println(esp_err_to_name(err));
    return false;
  }

  Serial.println("      802.11 management capture: ENABLED");
  Serial.printf("      Capture target BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                targetBSSID[0], targetBSSID[1], targetBSSID[2],
                targetBSSID[3], targetBSSID[4], targetBSSID[5]);
  Serial.printf("      Capture STA MAC:      %02X:%02X:%02X:%02X:%02X:%02X\n",
                stationMAC[0], stationMAC[1], stationMAC[2],
                stationMAC[3], stationMAC[4], stationMAC[5]);
  return true;
}

void end() {
  if (!enabled) {
    return;
  }

  enabled = false;
  esp_err_t err = esp_wifi_set_promiscuous(false);
  if (err != ESP_OK) {
    Serial.print("      802.11 capture: disable warning: ");
    Serial.println(esp_err_to_name(err));
  }
}

void print() {
  Serial.println();
  Serial.println("      802.11 management-frame capture:");
  const size_t captured = count;

  if (captured == 0) {
    Serial.println("      <no matching management frames received>");
    Serial.println("      This means the ESP32 did not observe an AP response on the selected channel.");
    return;
  }

  for (size_t i = 0; i < captured; ++i) {
    Entry entry;
    portENTER_CRITICAL(&mux);
    entry = entries[i];
    portEXIT_CRITICAL(&mux);

    Serial.printf(
        "      [%lu ms] %s  RSSI %d dBm  CH %u  len %u  seq %u",
        static_cast<unsigned long>(entry.timestampMs),
        subtypeName(entry.subtype),
        static_cast<int>(entry.rssi),
        static_cast<unsigned>(entry.channel),
        static_cast<unsigned>(entry.length),
        static_cast<unsigned>(entry.sequence));

    if (entry.code != 0xFFFF) {
      Serial.printf("  code %u", static_cast<unsigned>(entry.code));
    }

    Serial.printf(
        "  SRC %02X:%02X:%02X:%02X:%02X:%02X"
        "  DST %02X:%02X:%02X:%02X:%02X:%02X"
        "  BSSID %02X:%02X:%02X:%02X:%02X:%02X\n",
        entry.source[0], entry.source[1], entry.source[2],
        entry.source[3], entry.source[4], entry.source[5],
        entry.destination[0], entry.destination[1], entry.destination[2],
        entry.destination[3], entry.destination[4], entry.destination[5],
        entry.bssid[0], entry.bssid[1], entry.bssid[2],
        entry.bssid[3], entry.bssid[4], entry.bssid[5]);
  }
}
}

const char* disconnectReasonName(uint8_t reason);

const char* pmfModeName(const wifi_pmf_config_t& pmf) {
  if (pmf.required) return "REQUIRED";
  if (pmf.capable) return "OPTIONAL";
  return "DISABLED";
}

void printStationSecurityConfig() {
  wifi_config_t config = {};
  const esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &config);

  if (err != ESP_OK) {
    Serial.print("Station security config: unavailable (");
    Serial.print(esp_err_to_name(err));
    Serial.println(")");
    return;
  }

  Serial.print("Station auth threshold: ");
  Serial.println(authModeName(config.sta.threshold.authmode));
  Serial.print("Station PMF: ");
  Serial.println(pmfModeName(config.sta.pmf_cfg));
  Serial.print("Station BSSID pinning: ");
  Serial.println(config.sta.bssid_set ? "ENABLED" : "disabled");
  Serial.print("Station channel hint: ");
  Serial.println(config.sta.channel);
}

void printWiFiEventTrace() {
  Serial.println();
  Serial.println("WiFi event sequence");
  Serial.println("-------------------");

  if (wifiEventTraceCount == 0) {
    Serial.println("No Arduino WiFi events captured.");
    return;
  }

  for (uint8_t i = 0; i < wifiEventTraceCount; ++i) {
    Serial.print("  +");
    Serial.print(wifiEventTrace[i].elapsedMs);
    Serial.print(" ms  ");
    Serial.print(wifiEventName(wifiEventTrace[i].eventId));

    if (wifiEventTrace[i].eventId == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.print("  reason=");
      Serial.print(wifiEventTrace[i].reason);
      Serial.print(" (");
      Serial.print(disconnectReasonName(wifiEventTrace[i].reason));
      Serial.print(")");
    }

    Serial.println();
  }

  bool sawStart = false;
  bool sawConnected = false;
  bool sawAuthModeChange = false;
  bool sawGotIP = false;

  for (uint8_t i = 0; i < wifiEventTraceCount; ++i) {
    switch (wifiEventTrace[i].eventId) {
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

  if (!sawConnected) {
    Serial.println("  Diagnostic: STA never reached the connected/association event.");
  } else if (!sawGotIP) {
    Serial.println("  Diagnostic: association event occurred, but IPv4 was not acquired.");
  }
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

  Serial.printf("      Target AP: %02X:%02X:%02X:%02X:%02X:%02X  CH %ld  RSSI %d dBm  %s\n",
                target.bssid[0], target.bssid[1], target.bssid[2],
                target.bssid[3], target.bssid[4], target.bssid[5],
                static_cast<long>(target.channel),
                static_cast<int>(target.rssi),
                authModeName(target.auth));
}

bool findStrongestAP(const String& ssid, TargetAP& target) {
  target = TargetAP{};

  Serial.println("      Scanning for matching access points...");
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  delay(50);
  WiFi.disconnect(false, false);
  delay(100);

  const int count = WiFi.scanNetworks();
  if (count < 0) {
    Serial.println("      ERROR: WiFi scan failed while resolving target AP.");
    WiFi.scanDelete();
    return false;
  }

  for (int i = 0; i < count; ++i) {
    if (WiFi.SSID(i) != ssid) {
      continue;
    }

    const int8_t rssi = WiFi.RSSI(i);
    if (!target.valid || rssi > target.rssi) {
      target.valid = true;
      target.channel = WiFi.channel(i);
      target.rssi = rssi;
      target.auth = WiFi.encryptionType(i);

      const uint8_t* bssid = WiFi.BSSID(i);
      if (bssid != nullptr) {
        memcpy(target.bssid, bssid, 6);
      } else {
        target.valid = false;
      }
    }
  }

  WiFi.scanDelete();

  if (!target.valid) {
    Serial.print("      No matching AP found for SSID: ");
    Serial.println(ssid);
    return false;
  }

  Serial.println("      Strongest matching AP selected:");
  printTargetAP(target);
  return true;
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

const char* disconnectReasonName(uint8_t reason) {
  switch (reason) {
    case 1: return "UNSPECIFIED";
    case 2: return "AUTH_EXPIRE";
    case 3: return "AUTH_LEAVE";
    case 4: return "ASSOC_EXPIRE";
    case 5: return "ASSOC_TOOMANY";
    case 6: return "NOT_AUTHED";
    case 7: return "NOT_ASSOCED";
    case 8: return "ASSOC_LEAVE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 23: return "IEEE802_1X_AUTH_FAILED";
    case 24: return "CIPHER_SUITE_REJECTED";
    case 34: return "HANDSHAKE_TIMEOUT";
    case 53: return "INVALID_PMKID";
    case 204: return "SAE_HASH_TO_ELEMENT";
    case 205: return "SAE_PK";
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

  // This connection routine is deliberately single-attempt. Arduino-ESP32
  // normally performs an implicit retry after the first disconnect; the
  // build-time STA patch makes that retry honor this setting.
  const bool previousAutoReconnect = WiFi.getAutoReconnect();
  WiFi.setAutoReconnect(false);

  TargetAP target;
  if (requestedTarget != nullptr && requestedTarget->valid) {
    target = *requestedTarget;
    Serial.println();
    Serial.println("WiFi connection target");
    Serial.println("----------------------");
    Serial.println("Using AP selected by the scan; BSSID/channel are pinned for this attempt.");
  } else {
    Serial.println();
    Serial.println("WiFi connection target");
    Serial.println("----------------------");
    Serial.println("No AP was supplied by the caller; resolving the strongest matching BSSID.");
    if (!findStrongestAP(ssid, target)) {
      WiFi.setAutoReconnect(previousAutoReconnect);
      return ConnectResult::SSID_NOT_FOUND;
    }
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);

  if (!NetConfig::apply()) {
    Serial.println("WiFi: network configuration failed.");
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }

  Serial.println();
  Serial.println("WiFi connection");
  Serial.println("----------------");
  Serial.print("SSID:      ");
  Serial.println(ssid);
  Serial.print("Mode:      ");
  Serial.println(NetConfig::mode() == NetConfig::Mode::STATIC ? "STATIC" : "DHCP");
  Serial.print("Password:  ");
  Serial.println(password.isEmpty() ? "none (open network)" : "configured");
  Serial.print("ESP32 STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Minimum security policy: ");
  Serial.println(password.isEmpty() ? "OPEN (open-network test)" : "WPA2-PSK (authenticated network)");
  printWPA3Support();
  Serial.println();

  Serial.println("[1/6] Preparing WiFi station...");
  WiFi.disconnect(true, false);
  delay(150);

  lastDisconnectReason = 0;
  lastDisconnectRSSI = 0;
  disconnectEventCount = 0;
  memset((void*)lastDisconnectBSSID, 0, sizeof(lastDisconnectBSSID));
  resetWiFiEventTrace();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(false);
  Serial.println("      Station reset and ready.");
  Serial.println("      Automatic reconnect: DISABLED for this attempt.");

  Serial.println("[2/6] Applying network configuration...");
  if (!NetConfig::apply()) {
    Serial.println("      FAILED: network configuration could not be applied.");
    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::NETWORK_CONFIG_FAILED;
  }
  Serial.println("      Network configuration applied.");

  const wifi_auth_mode_t minimumSecurity =
      password.isEmpty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  WiFi.setMinSecurity(minimumSecurity);

  Serial.println("[3/6] Starting connection attempt...");
  Serial.print("      Configured minimum security: ");
  Serial.println(authModeName(minimumSecurity));
  printTargetAP(target);
  Serial.print("      BSSID/channel pinning: ");
  Serial.println(NetConfig::DIAGNOSTIC_PIN_BSSID ? "ENABLED" : "DISABLED");
  Serial.println("      Station security configuration before WiFi.begin():");
  printStationSecurityConfig();

  if (WIFI_DIAGNOSTICS) {
    if (!WiFiFrameCapture::begin(target.bssid)) {
      Serial.println("      WARNING: 802.11 management capture could not be enabled.");
    }
  }

  // The scan selects the strongest matching AP. Pin that exact BSSID/channel so
  // another AP advertising the same SSID cannot be selected during association.
  if (NetConfig::DIAGNOSTIC_PIN_BSSID) {
    WiFi.begin(ssid.c_str(), password.c_str(),
               target.channel, target.bssid, true);
  } else {
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  Serial.println("      WiFi.begin() accepted.");
  Serial.println("      Station security configuration after WiFi.begin():");
  printStationSecurityConfig();

  Serial.println("[4/6] Waiting for association/authentication...");
  const uint32_t startTime = millis();
  wl_status_t lastStatus = WiFi.status();
  uint32_t lastReport = startTime;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < CONNECT_TIMEOUT_MS) {
    delay(250);

    if (Console::disconnected()) {
      Serial.println("      Serial session disconnected during WiFi connection attempt.");
      Serial.println("      Aborting connection attempt.");
      WiFi.disconnect(true, false);
      delay(100);
      WiFi.setAutoReconnect(previousAutoReconnect);
      return ConnectResult::SERIAL_DISCONNECTED;
    }

    const wl_status_t currentStatus = WiFi.status();
    if (currentStatus != lastStatus || millis() - lastReport >= 2000) {
      Serial.print("      status=");
      Serial.print(static_cast<int>(currentStatus));
      Serial.print("  elapsed=");
      Serial.print((millis() - startTime) / 1000);
      Serial.println("s");
      lastStatus = currentStatus;
      lastReport = millis();
    }
  }

  const wl_status_t finalStatus = WiFi.status();

  if (finalStatus == WL_CONNECTED) {
    if (WIFI_DIAGNOSTICS) {
      WiFiFrameCapture::end();
    }

    Serial.println("[5/6] Associated and authenticated.");
    Serial.print("      Connected BSSID: ");
    Serial.println(WiFi.BSSIDstr());
    Serial.print("      Connected channel: ");
    Serial.println(WiFi.channel());
    Serial.println("[6/6] Network address acquired.");
    printStatus();

    preferences.putString(SSID_KEY, ssid);
    preferences.putString(PASSWORD_KEY, password);
    Serial.println("WiFi credentials committed to NVS.");
    Serial.println("Connection result: SUCCESS");

    WiFi.setAutoReconnect(previousAutoReconnect);
    return ConnectResult::SUCCESS;
  }

  if (WIFI_DIAGNOSTICS) {
    WiFiFrameCapture::end();
  }

  Serial.println("[5/6] Connection attempt did not complete.");
  Serial.print("      Final WiFi status: ");
  Serial.println(static_cast<int>(finalStatus));

  ConnectResult result = ConnectResult::UNKNOWN;

  if (disconnectEventCount != 0) {
    Serial.print("      802.11 disconnect events captured: ");
    Serial.println(static_cast<unsigned>(disconnectEventCount));
    Serial.print("      Last 802.11 disconnect reason: ");
    Serial.print(static_cast<unsigned>(lastDisconnectReason));
    Serial.print(" (");
    Serial.print(disconnectReasonName(lastDisconnectReason));
    Serial.print(")  RSSI: ");
    Serial.print(static_cast<int>(lastDisconnectRSSI));
    Serial.println(" dBm");

    Serial.print("      BSSID at disconnect: ");
    char disconnectBSSID[18];
    snprintf(disconnectBSSID, sizeof(disconnectBSSID),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             lastDisconnectBSSID[0], lastDisconnectBSSID[1],
             lastDisconnectBSSID[2], lastDisconnectBSSID[3],
             lastDisconnectBSSID[4], lastDisconnectBSSID[5]);
    Serial.println(disconnectBSSID);

    if (lastDisconnectReason == 2) {
      result = ConnectResult::AUTH_EXPIRED;
      Serial.println("      Authentication response timed out.");
      Serial.println("      Password validity: NOT DETERMINED.");
    } else if (lastDisconnectReason == 15 ||
               lastDisconnectReason == 34 ||
               lastDisconnectReason == 23 ||
               lastDisconnectReason == 24) {
      result = ConnectResult::HANDSHAKE_FAILED;
      Serial.println("      WPA/802.1X handshake failed.");
      Serial.println("      Password may be incorrect, but this is not treated as proof.");
    } else if (lastDisconnectReason == 3 ||
               lastDisconnectReason == 8) {
      result = ConnectResult::CONNECTION_LOST;
    }
  }

  if (result == ConnectResult::UNKNOWN) {
    if (finalStatus == WL_NO_SSID_AVAIL) {
      result = ConnectResult::SSID_NOT_FOUND;
      Serial.println("      Diagnosis: SSID is not currently available.");
    } else if (finalStatus == WL_CONNECTION_LOST) {
      result = ConnectResult::CONNECTION_LOST;
      Serial.println("      Diagnosis: connection was established then lost.");
    } else if (millis() - startTime >= CONNECT_TIMEOUT_MS) {
      result = ConnectResult::TIMEOUT;
      Serial.println("      Diagnosis: connection attempt timed out.");
    } else if (finalStatus == WL_CONNECT_FAILED) {
      result = ConnectResult::AUTH_FAILED;
      Serial.println("      Diagnosis: association/authentication failed.");
      Serial.println("      Password validity: NOT DETERMINED unless a WPA handshake failure is reported.");
    } else {
      result = ConnectResult::UNKNOWN;
      Serial.println("      Diagnosis: see final status and 802.11 reason above.");
    }
  }

  Serial.print("      Connection result: ");
  Serial.println(connectResultName(result));

  printWiFiEventTrace();
  if (WIFI_DIAGNOSTICS) {
    WiFiFrameCapture::print();
  }

  Serial.println("[6/6] Resetting WiFi after failed connection...");
  if (!WiFi.disconnect(true, false)) {
    Serial.println("      WARNING: WiFi radio shutdown reported failure.");
  }
  delay(150);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(previousAutoReconnect);
  Serial.println("      WiFi station reset complete; ready for retry or rescan.");

  return result;
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

    // Leave the STA interface completely reset so a failed scan cannot
    // contaminate the next connection attempt. STA mode is already active.
    WiFi.disconnect(true, false);
    delay(150);
    WiFi.mode(WIFI_STA);
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

  String choice = Console::readPrompt("Select (B=Back): ");
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
  // The selected BSSID is diagnostic information only; credentials are not
  // bound to that AP, so normal mesh/AP selection remains available.
  
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

    String choice = Console::readPrompt("Select: ");
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
    } else {
      Serial.println("Unknown selection.");
    }
  }
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);
  WiFi.onEvent(onWiFiEvent);
  Serial.println();
  Serial.println("WiFi subsystem starting...");
  connect();
}
}

namespace Setup {
void menu() {
  while (true) {
    Serial.println();
    if (Console::ansiSupported) {
      Console::color("1;33m");
      Serial.println("+---------------------------+");
      Serial.println("|            SETUP          |");
      Serial.println("+---------------------------+");
      Console::resetStyle();
      Console::color("1;36m");
    } else {
      Serial.println("SETUP");
      Serial.println("=====");
    }
    Serial.println("1. WiFi");
    Serial.println("2. Network");
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readPrompt("Select: ");
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      WiFiControl::menu();
    } else if (choice == "2") {
      NetConfig::menu();
    } else if (choice == "B") {
      return;
    } else {
      Serial.println("Unknown selection.");
    }
  }
}
}

namespace MainMenu {
void print() {
  if (Console::ansiSupported) {
    Console::clearScreen();
    Console::color("1;36m");
    Serial.println("+================================+");
    Console::color("1;37m");
    Serial.println("|     ESP32-C3 PC RELAY CONTROL  |");
    Console::color("1;36m");
    Console::resetStyle();
    Serial.print("Firmware build: ");
    Serial.println(FIRMWARE_BUILD_VERSION);
    Console::color("1;36m");
    Serial.println("+================================+");
    Console::resetStyle();
    Console::color("1;32m"); Serial.println("  1  Status");
    Console::color("1;33m"); Serial.println("  2  Setup");
    Console::color("1;34m"); Serial.println("  3  Reconnect WiFi");
    Console::color("1;31m"); Serial.println("  Q  Quit menu");
    Console::resetStyle();
  } else {
    Serial.println("ESP32-C3 PC RELAY CONTROLLER");
    Serial.println("============================");
    Serial.print("Firmware build: ");
    Serial.println(FIRMWARE_BUILD_VERSION);
    Serial.println("1. Status");
    Serial.println("2. Setup");
    Serial.println("3. Reconnect WiFi");
    Serial.println("Q. Quit menu");
  }
  Serial.println();
}

void showStatus() {
  Serial.println();
  if (Console::ansiSupported) {
    Console::color("1;36m");
    Serial.println("+=============================+");
    Serial.println("|        SYSTEM STATUS        |");
    Serial.println("+=============================+");
    Console::resetStyle();
  } else {
    Serial.println("SYSTEM STATUS");
    Serial.println("=============");
  }
  WiFiControl::printStatus();
  NetConfig::printSettings();
}

void loop() {
  while (true) {
    if (Console::disconnected()) {
      // Treat a COM-port loss as a brand-new console session.
      Console::resetTransport();
      Console::waitForConnection();
      Console::begin();
      continue;
    }

    print();

    String choice = Console::readPrompt("Select: ");
    if (Console::disconnected()) {
      Console::resetTransport();
      Console::waitForConnection();
      Console::begin();
      continue;
    }
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      showStatus();
    } else if (choice == "2") {
      Setup::menu();
    } else if (choice == "3") {
      WiFiControl::connect();
    } else if (choice == "Q") {
      return;
    } else {
      Serial.println("Please select 1, 2, 3, or Q.");
    }
  }
}
}

void setup() {
  Relay::begin();

  Serial.begin(115200);
  // Keep native USB-Serial/JTAG writes bounded when the host disappears.
  Serial.setTxTimeoutMs(50);
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.onEvent(ARDUINO_HW_CDC_BUS_RESET_EVENT, Console::onHardwareCDCEvent);
#endif
  delay(250);

  Serial.println();
  Serial.println("ESP32-C3 PC Relay Controller");
  Serial.println("Relay test firmware - no automatic pulses");
  Serial.println("POWER=GPIO5, RESET=GPIO6");
  Serial.print("Firmware build: ");
  Serial.println(FIRMWARE_BUILD_VERSION);

  Console::begin();

  NetConfig::begin();
  WiFiControl::begin();

  Serial.println();
  Serial.println("Serial configuration console ready.");
}

void loop() {
  MainMenu::loop();
}