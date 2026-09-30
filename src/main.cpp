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

void detectANSI() {
  // VT/xterm-compatible terminals answer the Device Attributes query.
  // If nothing answers, keep the console in plain-text mode.
  while (Serial.available()) {
    Serial.read();
  }

  Serial.write(0x1B);
  Serial.print("[c");
  Serial.flush();

  const uint32_t start = millis();
  String response;
  while (millis() - start < 500) {
    while (Serial.available()) {
      char c = static_cast<char>(Serial.read());
      response += c;

      // A Device Attributes response begins with ESC[ and ends in 'c'.
      if (response.length() >= 3 &&
          response[0] == 27 && response[1] == '[' && c == 'c') {
        ansiSupported = true;
        return;
      }
    }
    delay(5);
  }
}

void begin() {
  detectANSI();
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

void configureStatic() {
  Serial.println();
  Serial.println("Manual / static IPv4 configuration");
  Serial.println("----------------------------------");

  String ip = Console::readPrompt("IP address: ");
  String gateway = Console::readPrompt("Gateway: ");
  String subnet = Console::readPrompt("Subnet mask: ");
  String dns1 = Console::readPrompt("DNS 1: ");
  String dns2 = Console::readPrompt("DNS 2: ");

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

  Serial.print("WiFi: connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi: CONNECTED");
    printStatus();
    return true;
  }

  Serial.print("WiFi: connection failed, status=");
  Serial.println(static_cast<int>(WiFi.status()));
  return false;
}

void printGridSeparator(size_t ssidWidth) {
  Serial.print("+-----+");
  for (size_t i = 0; i < ssidWidth + 2; ++i) {
    Serial.print("-");
  }
  Serial.print("+--------+----+------------+-------------------+");
  Serial.println();
}

void printScanGrid(int count) {
  size_t ssidWidth = 4;

  for (int i = 0; i < count; ++i) {
    const size_t length = WiFi.SSID(i).length();
    if (length > ssidWidth) {
      ssidWidth = length;
    }
  }

  Serial.println();
  printGridSeparator(ssidWidth);
  Serial.print("| #   | SSID");
  for (size_t i = 4; i < ssidWidth; ++i) {
    Serial.print(" ");
  }
  Serial.println(" | RSSI   | CH | SECURITY   | BSSID             |");
  printGridSeparator(ssidWidth);

  for (int i = 0; i < count; ++i) {
    String ssid = WiFi.SSID(i);
    String security =
        WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED";
    String bssid = WiFi.BSSIDstr(i);

    Serial.printf("| %-3d | ", i + 1);
    Serial.print(ssid);

    for (size_t padding = ssid.length(); padding < ssidWidth; ++padding) {
      Serial.print(" ");
    }

    Serial.print(" | ");
    Serial.printf("%-6d | %-2d | %-10s | %-17s |\n",
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  security.c_str(),
                  bssid.c_str());
  }

  printGridSeparator(ssidWidth);
}

void scan() {
  Serial.println();
  Serial.println("WiFi scan");
  Serial.println("---------");
  Serial.println("Scanning nearby networks...");

  WiFi.mode(WIFI_STA);
  int count = WiFi.scanNetworks();

  if (count < 0) {
    Serial.println("WiFi scan failed.");
    WiFi.scanDelete();
    return;
  }

  if (count == 0) {
    Serial.println("No networks found.");
    WiFi.scanDelete();
    return;
  }

  printScanGrid(count);

  Serial.println();
  Serial.println("B. Back");
  String choice = Console::readPrompt("Select (B=Back): ");
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

  if (selected < 1 || selected > count) {
    Serial.println("Invalid network selection.");
    WiFi.scanDelete();
    return;
  }

  const int index = selected - 1;
  String ssid = WiFi.SSID(index);
  wifi_auth_mode_t encryption = WiFi.encryptionType(index);

  Serial.println();
  Serial.print("Selected SSID: ");
  Serial.println(ssid);

  Serial.print("BSSID: ");
  Serial.println(WiFi.BSSIDstr(index));

  if (encryption == WIFI_AUTH_OPEN) {
    Serial.println("Security: OPEN");
    Serial.println("No password required.");
  } else {
    Serial.println("Security: password required.");
    String password = Console::readPrompt("PASSWORD: ");

    WiFi.scanDelete();

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

  WiFi.scanDelete();

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
  String password = Console::readPrompt("PASSWORD: ");

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
    print();

    String choice = Console::readPrompt("Select: ");
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
  Serial.print("Terminal mode: ");
  Serial.println(Console::ansiSupported ? "ANSI detected" : "plain text");

  Network::begin();
  WiFiControl::begin();

  Serial.println();
  Serial.println("Serial configuration console ready.");
}

void loop() {
  MainMenu::loop();
}
