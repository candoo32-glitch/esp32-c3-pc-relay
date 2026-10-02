#include <Arduino.h>
#include "Setup.h"
#include "../interface/Console.h"
#include "../wifi/WiFiControl.h"
#include "../network/NetConfig.h"
#include "../storage/NVSControl.h"

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
    Serial.println("3. NVS");
    if (Console::ansiSupported) Console::color("1;35m");
    Serial.println("4. Reboot");
    if (Console::ansiSupported) Console::resetStyle();
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readMenuChoice("Select: ", "1234B");
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      WiFiControl::menu();
    } else if (choice == "2") {
      NetConfig::menu();
    } else if (choice == "3") {
      NVSControl::menu();
    } else if (choice == "4") {
      Serial.println();
      if (Console::ansiSupported) Console::color("1;35m");
      Serial.println("Rebooting ESP32-C3...");
      if (Console::ansiSupported) Console::resetStyle();
      delay(250);
      ESP.restart();
    } else if (choice == "B") {
      return;
    }
  }
}
}
