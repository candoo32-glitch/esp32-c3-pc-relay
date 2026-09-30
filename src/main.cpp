#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

namespace Pins {
constexpr uint8_t POWER_RELAY = 5; // S1
constexpr uint8_t RESET_RELAY = 6; // S2
}

namespace Relay {
constexpr uint8_t OFF = HIGH;
constexpr uint8_t ON  = LOW;

void begin() {
  digitalWrite(Pins::POWER_RELAY, OFF);
  digitalWrite(Pins::RESET_RELAY, OFF);
  pinMode(Pins::POWER_RELAY, OUTPUT);
  pinMode(Pins::RESET_RELAY, OUTPUT);
}
}

namespace Console {
bool ansiSupported = false;
volatile bool sessionLost = false;

bool connected() {
  return Serial.isConnected();
}

bool disconnected() {
  if (!connected()) {
    sessionLost = true;
  }
  return sessionLost;
}

void waitForConnection() {
  while (!connected()) {
    delay(50);
  }
  sessionLost = false;
}

void resetSession() {
  sessionLost = false;
  ansiSupported = false;
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

  while (true) {
    if (disconnected()) {
      sessionLost = true;
      return "";
    }

    while (Serial.available()) {
      char c = static_cast<char>(Serial.read());

      if (c == '\r' || c == '\n') {
        // Treat either CR or LF as Enter. PuTTY commonly sends CR for
        // the Return key, while other terminals may send LF or CRLF.
        Serial.println();
        return value;
      }

      if (c == '\b' || c == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);

          // Erase one character on ANSI terminals. Plain terminals still
          // receive a best-effort backspace sequence.
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      // Echo typed characters so the ESP32 console works even when the
      // terminal application's local echo is disabled.
      Serial.write(c);
      value += c;
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
}

namespace Network {
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

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)event;
  lastDisconnectReason = info.wifi_sta_disconnected.reason;
  lastDisconnectRSSI = info.wifi_sta_disconnected.rssi;
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
  String ssid = preferences.getString(SSID_KEY, "");

  if (ssid.isEmpty()) {
    Serial.println("WiFi: not configured.");
    return false;
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);

  if (!Network::apply()) {
    Serial.println("WiFi: network configuration failed.");
    return false;
  }

  String password = preferences.getString(PASSWORD_KEY, "");

  Serial.println();
  Serial.println("WiFi connection");
  Serial.println("----------------");
  Serial.print("SSID:      ");
  Serial.println(ssid);
  Serial.print("Mode:      ");
  Serial.println(Network::mode() == Network::Mode::STATIC ? "STATIC" : "DHCP");
  Serial.print("Password:  ");
  Serial.println(password.isEmpty() ? "none (open network)" : "configured");
  printWPA3Support();
  Serial.println();

  Serial.println("[1/6] Preparing WiFi station...");
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  Serial.println("      Station ready.");

  Serial.println("[2/6] Applying network configuration...");
  if (!Network::apply()) {
    Serial.println("      FAILED: network configuration could not be applied.");
    return false;
  }
  Serial.println("      Network configuration applied.");

  lastDisconnectReason = 0;
  lastDisconnectRSSI = 0;

  Serial.println("[3/6] Starting connection attempt...");
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("      WiFi.begin() accepted.");

  Serial.println("[4/6] Waiting for association/authentication...");
  const uint32_t start = millis();
  wl_status_t lastStatus = WiFi.status();
  uint32_t lastReport = start;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < CONNECT_TIMEOUT_MS) {
    delay(250);

    const wl_status_t currentStatus = WiFi.status();
    if (currentStatus != lastStatus || millis() - lastReport >= 2000) {
      Serial.print("      status=");
      Serial.print(static_cast<int>(currentStatus));
      Serial.print("  elapsed=");
      Serial.print((millis() - start) / 1000);
      Serial.println("s");
      lastStatus = currentStatus;
      lastReport = millis();
    }
  }

  const wl_status_t finalStatus = WiFi.status();

  if (finalStatus == WL_CONNECTED) {
    Serial.println("[5/6] Associated and authenticated.");
    Serial.println("[6/6] Network address acquired.");
    printStatus();
    return true;
  }

  Serial.println("[5/6] Connection attempt did not complete.");
  Serial.print("      Final WiFi status: ");
  Serial.println(static_cast<int>(finalStatus));

  if (lastDisconnectReason != 0) {
    Serial.print("      802.11 disconnect reason: ");
    Serial.print(static_cast<unsigned>(lastDisconnectReason));
    Serial.print("  RSSI at disconnect: ");
    Serial.print(static_cast<int>(lastDisconnectRSSI));
    Serial.println(" dBm");
    Serial.println("      This is the AP/802.11 reason, which is more specific than WL status 6.");
  } else {
    Serial.println("      No 802.11 disconnect reason was captured.");
  }

  if (finalStatus == WL_NO_SSID_AVAIL) {
    Serial.println("      Diagnosis: SSID is not currently available.");
  } else if (finalStatus == WL_CONNECT_FAILED) {
    Serial.println("      Diagnosis: association/authentication failed.");
    Serial.println("      This does NOT by itself prove the password is wrong.");
  } else if (finalStatus == WL_CONNECTION_LOST) {
    Serial.println("      Diagnosis: connection was established then lost.");
  } else if (finalStatus == WL_IDLE_STATUS) {
    Serial.println("      Diagnosis: WiFi remained idle.");
  } else {
    Serial.println("      Diagnosis: see final status code above.");
  }

  Serial.println("[6/6] Clearing transient connection state...");
  WiFi.disconnect(false, false);
  delay(100);
  Serial.println("      Ready for retry or rescan.");

  return false;
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
  Network::configureDHCP();

  // Always start a fresh scan from a clean STA state. This is especially
  // important after a failed password/connection attempt, which can leave
  // the WiFi state machine in a transient connecting/failed state.
  WiFi.scanDelete();
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_STA);
  int count = WiFi.scanNetworks();

  if (count < 0) {
    Serial.println("WiFi scan failed.");
    WiFi.scanDelete();

    // Leave the STA interface in a clean idle state so the next scan or
    // connection attempt starts from a known state.
    WiFi.disconnect(false, false);
    delay(100);
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
    String password = Console::readPrompt("PASSWORD: ");
    if (Console::disconnected()) {
      WiFi.scanDelete();
      return;
    }

    WiFi.scanDelete();

    // Selecting a network from a scan always uses DHCP for the connection.
    Network::configureDHCP();
    preferences.putString(SSID_KEY, ssid);
    preferences.putString(PASSWORD_KEY, password);

    Serial.println();
    Serial.println("WiFi credentials saved.");
    Serial.println("Connecting...");

    if (connect()) {
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
      Serial.println("Credentials were saved.");
      Serial.println("You can try again from WiFi setup.");
      Serial.println();
    }
    return;
  }

  // Keep the selected AP details visible through the credential prompt.
  // The normal connection still uses SSID/password only so an Orbi/mesh
  // network remains free to roam between its access points.
  
  // Selecting a network from a scan always uses DHCP for the connection.
  WiFi.scanDelete();
  Network::configureDHCP();
  preferences.putString(SSID_KEY, ssid);
  preferences.putString(PASSWORD_KEY, "");

  Serial.println();
  Serial.println("WiFi credentials saved.");
  Serial.println("Connecting...");

  if (connect()) {
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
    Serial.println("Credentials were saved.");
    Serial.println("You can try again from WiFi setup.");
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
  String password = Console::readPrompt("PASSWORD: ");
  if (Console::disconnected()) return;

  // Entering SSID/password always starts from DHCP.
  Network::configureDHCP();

  if (ssid.isEmpty()) {
    Serial.println("WiFi: SSID cannot be empty. Nothing was changed.");
    return;
  }

  preferences.putString(SSID_KEY, ssid);
  preferences.putString(PASSWORD_KEY, password);

  Serial.println();
  Serial.println("WiFi credentials saved.");
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
  WiFi.onEvent(onWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
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
      Network::menu();
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
  Serial.println();
  if (Console::ansiSupported) {
    Console::clearScreen();
    Console::color("1;36m");
    Serial.println("+================================+");
    Console::color("1;37m");
    Serial.println("|     ESP32-C3 PC RELAY CONTROL  |");
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
  Network::printSettings();
}

void loop() {
  while (true) {
    if (Console::disconnected()) {
      // Reset only the console session; keep the ESP32 running.
      Console::waitForConnection();
      Console::begin();
      continue;
    }

    print();

    String choice = Console::readPrompt("Select: ");
    if (Console::disconnected()) {
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
  delay(250);

  Serial.println();
  Serial.println("ESP32-C3 PC Relay Controller");
  Serial.println("Relay test firmware - no automatic pulses");
  Serial.println("POWER=GPIO5, RESET=GPIO6");

  Console::begin();

  Network::begin();
  WiFiControl::begin();

  Serial.println();
  Serial.println("Serial configuration console ready.");
}

void loop() {
  MainMenu::loop();
}
