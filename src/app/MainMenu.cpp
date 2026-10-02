#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_idf_version.h>
#include "MainMenu.h"
#include "Setup.h"
#include "../interface/Console.h"
#include "../wifi/WiFiControl.h"
#include "../network/NetConfig.h"
#include "../wifi/WiFiDiagnostics.h"

#ifndef FW_BUILD_VERSION
#define FW_BUILD_VERSION 0
#endif
constexpr uint32_t FIRMWARE_BUILD_VERSION = FW_BUILD_VERSION;

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
    Serial.println("+==================================================+");
    Console::color("1;37m");
    Serial.println("|                  SYSTEM STATUS                   |");
    Console::color("1;36m");
    Serial.println("+==================================================+");
    Console::resetStyle();

    Console::color("1;35m");
    Serial.println("  FIRMWARE");
    Console::resetStyle();
    Serial.print("  Version       : ");
    Console::color("1;32m"); Serial.println(FIRMWARE_BUILD_VERSION); Console::resetStyle();
    Serial.print("  Build         : ");
    Console::color("1;33m"); Serial.println(String(__DATE__) + " " + String(__TIME__)); Console::resetStyle();
    Serial.print("  IDF           : ");
    Console::color("1;36m"); Serial.println(String(ESP_IDF_VERSION_MAJOR) + "." + String(ESP_IDF_VERSION_MINOR) + "." + String(ESP_IDF_VERSION_PATCH)); Console::resetStyle();
    Serial.print("  Arduino core  : ");
    Console::color("1;36m"); Serial.println(ESP_ARDUINO_VERSION_STR); Console::resetStyle();
    Serial.print("  Chip          : ");
    Console::color("1;36m"); Serial.println("ESP32-C3"); Console::resetStyle();
    Serial.print("  CPU frequency : ");
    Serial.print("  ");
    Console::color("1;37m");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");
    Console::resetStyle();

    Console::color("1;35m");
    Serial.println("  NETWORK");
    Console::resetStyle();
    WiFiControl::printStatus();
    NetConfig::printSettings();

    Console::color("1;36m");
    Serial.println("+==================================================+");
    Console::resetStyle();
  } else {
    Serial.println("SYSTEM STATUS");
    Serial.println("=============");
    Serial.println("FIRMWARE");
    Serial.print("Version: "); Serial.println(FIRMWARE_BUILD_VERSION);
    Serial.print("Build: "); Serial.println(String(__DATE__) + " " + String(__TIME__));
    Serial.print("IDF: "); Serial.println(String(ESP_IDF_VERSION_MAJOR) + "." + String(ESP_IDF_VERSION_MINOR) + "." + String(ESP_IDF_VERSION_PATCH));
    Serial.print("Arduino core: "); Serial.println(ESP_ARDUINO_VERSION_STR);
    Serial.println("Chip: ESP32-C3");
    Serial.print("CPU frequency: "); Serial.print(getCpuFrequencyMhz()); Serial.println(" MHz");
    Serial.println();
    Serial.println("NETWORK");
    WiFiControl::printStatus();
    NetConfig::printSettings();
  }
}

void loop() {
  // USB is an optional management console. Never block the firmware waiting
  // for a terminal; headless operation must continue servicing the web server.
  if (!Console::connected()) {
    if (Serial.isConnected()) {
      Console::begin();
    } else {
      delay(10);
      return;
    }
  }

  while (true) {
    WiFiControl::service();
    WiFiDiagnostics::service();
    if (Console::disconnected()) {
      // Treat a COM-port loss as a brand-new console session.
      Console::resetTransport();
      Console::waitForConnection();
      Console::begin();
      continue;
    }

    print();

    String choice = Console::readMenuChoice("Select: ", "123Q");
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
      Serial.println();
      Console::readMenuChoice("Press B to return: ", "B");
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
