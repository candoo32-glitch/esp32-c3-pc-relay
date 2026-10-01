#include <Arduino.h>
#include "Setup.h"
#include "../interface/Console.h"
#include "../wifi/WiFiControl.h"
#include "../network/NetConfig.h"

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

    String choice = Console::readMenuChoice("Select: ", "12B");
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      WiFiControl::menu();
    } else if (choice == "2") {
      NetConfig::menu();
    } else if (choice == "B") {
      return;
    }
  }
}
}
