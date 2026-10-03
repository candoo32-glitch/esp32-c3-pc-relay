#pragma once

#include <esp_wifi.h>

namespace WiFiControl {
void begin();
void service();
bool connect();
bool isEnabled();
bool setEnabled(bool enabled);
bool diagnosticsPreference();
bool saveDiagnosticsPreference(bool enabled);
String savedSSID();
bool configureCredentials(const String& ssid, const String& password);
bool setTxPowerDbm(float dbm);
void menu();
void printStatus();
const char* authModeName(wifi_auth_mode_t authMode);
}
