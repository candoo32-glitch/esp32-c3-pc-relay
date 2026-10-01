#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace NetConfig {
enum class Mode : uint8_t { DHCP = 0, STATIC = 1 };
void begin();
Mode mode();
void printSettings();
bool parseIP(const String& text, IPAddress& address);
bool loadStatic(IPAddress& ip, IPAddress& gateway, IPAddress& subnet, IPAddress& dns1, IPAddress& dns2);
void configureDHCP();
String currentIP();
String currentGateway();
String currentSubnet();
String currentDNS1();
String currentDNS2();
void configureStatic();
void menu();
bool apply();
}
