#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
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
constexpr char HOSTNAME_KEY[] = "hostname";
constexpr char DEFAULT_HOSTNAME[] = "esp32-c3-relay";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
bool mdnsStarted = false;

Mode mode() {
  return preferences.getUChar(MODE_KEY, static_cast<uint8_t>(Mode::DHCP))
           == static_cast<uint8_t>(Mode::STATIC)
         ? Mode::STATIC
         : Mode::DHCP;
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);
  if (!preferences.isKey(HOSTNAME_KEY)) {
    preferences.putString(HOSTNAME_KEY, DEFAULT_HOSTNAME);
  }
  // Keep DNS 1 and DNS 2 as explicit NVS configuration values. These are
  // used by static/manual networking and are also available to the serial
  // configuration menu and web UI.
  if (!preferences.isKey(DNS1_KEY)) {
    preferences.putString(DNS1_KEY, "192.168.1.1");
  }
  if (!preferences.isKey(DNS2_KEY)) {
    preferences.putString(DNS2_KEY, "8.8.8.8");
  }
}

String hostname() {
  return preferences.getString(HOSTNAME_KEY, DEFAULT_HOSTNAME);
}

bool validHostname(const String& value) {
  if (value.isEmpty() || value.length() > 32) return false;
  if (value[0] == '-' || value[value.length() - 1] == '-') return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

bool setHostname(const String& value) {
  String candidate = value;
  candidate.trim();
  if (!validHostname(candidate)) return false;
  const size_t saved = preferences.putString(HOSTNAME_KEY, candidate);
  if (saved != candidate.length() + 1) return false;
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  WiFi.setHostname(candidate.c_str());
  return true;
}

void service() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    if (mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }
    return;
  }

  if (!mdnsStarted) {
    const String host = hostname();
    WiFi.setHostname(host.c_str());
    if (MDNS.begin(host.c_str())) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.print("mDNS: http://");
      Serial.print(host);
      Serial.println(".local/");
    } else {
      Serial.println("mDNS: failed to start.");
    }
  }
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

String savedIP() { return preferences.getString(IP_KEY, ""); }
String savedGateway() { return preferences.getString(GATEWAY_KEY, ""); }
String savedSubnet() { return preferences.getString(SUBNET_KEY, ""); }
String savedDNS1() { return preferences.getString(DNS1_KEY, ""); }
String savedDNS2() { return preferences.getString(DNS2_KEY, ""); }

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
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress dns2 = WiFi.dnsIP(1);
    if (dns2 != IPAddress()) return dns2.toString();
  }
  return preferences.getString(DNS2_KEY, "8.8.8.8");
}

bool saveStatic(const String& ip, const String& gateway, const String& subnet,
                 const String& dns1, const String& dns2) {
  IPAddress testIP, testGateway, testSubnet, testDNS1, testDNS2;
  if (!parseIP(ip, testIP) || !parseIP(gateway, testGateway) ||
      !parseIP(subnet, testSubnet) || !parseIP(dns1, testDNS1) ||
      !parseIP(dns2, testDNS2)) {
    return false;
  }

  if (ip.length() > 15 || gateway.length() > 15 || subnet.length() > 15 ||
      dns1.length() > 15 || dns2.length() > 15) {
    return false;
  }

  preferences.putString(IP_KEY, ip);
  preferences.putString(GATEWAY_KEY, gateway);
  preferences.putString(SUBNET_KEY, subnet);
  preferences.putString(DNS1_KEY, dns1);
  preferences.putString(DNS2_KEY, dns2);
  preferences.putUChar(MODE_KEY, static_cast<uint8_t>(Mode::STATIC));
  return true;
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
  WiFi.setHostname(hostname().c_str());

  if (mode() == Mode::STATIC) {
    IPAddress ip, gateway, subnet, dns1, dns2;

    if (!loadStatic(ip, gateway, subnet, dns1, dns2)) {
      return false;
    }

    Serial.println("Network: using manual/static IPv4.");
    return WiFi.config(ip, gateway, subnet, dns1, dns2);
  }

  // Explicitly clear any previously applied static IPv4 configuration.
  // Selecting DHCP must be able to undo a prior manual/static address in the
  // live network interface, not merely change the saved NVS mode.
  if (!WiFi.config(IPAddress(), IPAddress(), IPAddress(), IPAddress(), IPAddress())) {
    Serial.println("ERROR: failed to switch live interface back to DHCP.");
    return false;
  }

  Serial.println("Network: using DHCP.");
  return true;
}
}
