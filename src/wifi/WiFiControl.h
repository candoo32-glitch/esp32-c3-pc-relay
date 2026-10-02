#pragma once

#include <esp_wifi.h>

namespace WiFiControl {
void begin();
void service();
bool connect();
bool diagnosticsPreference();
bool saveDiagnosticsPreference(bool enabled);
void menu();
void printStatus();
const char* authModeName(wifi_auth_mode_t authMode);
}
