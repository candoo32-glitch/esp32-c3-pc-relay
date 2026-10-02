#pragma once

#include <esp_wifi.h>

namespace WiFiControl {
void begin();
bool connect();
void menu();
void printStatus();
const char* authModeName(wifi_auth_mode_t authMode);
}

