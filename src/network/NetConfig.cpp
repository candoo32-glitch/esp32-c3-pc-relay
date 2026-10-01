#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "NetConfig.h"
#include "../interface/Console.h"

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
constexpr bool DIAGNOSTIC_PIN_BSSID = false;
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
  if (mode() != Mode::DHCP) {
    preferences.putUChar(MODE_KEY, static_cast<uint8_t>(Mode::DHCP));
    Serial.println("Network mode saved: DHCP.");
  } else {
    Serial.println("Network mode: DHCP (already saved).");
  }
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

    String choice = Console::readMenuChoice("Select: ", "123B");
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
