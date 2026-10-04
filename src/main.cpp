#include <Arduino.h>
#include "relay/Relay.h"
#include "interface/Console.h"
#include "network/NetConfig.h"
#include "wifi/WiFiControl.h"
#include "app/MainMenu.h"
#include "web/WebServerControl.h"

#ifndef FW_BUILD_VERSION
#define FW_BUILD_VERSION 0
#endif
constexpr uint32_t FIRMWARE_BUILD_VERSION = FW_BUILD_VERSION;

namespace {
uint32_t bootStartMs = 0;
uint32_t relayReadyMs = 0;
uint32_t serialReadyMs = 0;
uint32_t networkReadyMs = 0;
uint32_t wifiInitMs = 0;
uint32_t webServerReadyMs = 0;
uint32_t consoleReadyMs = 0;

void printBootTiming() {
  const uint32_t now = millis();

  Serial.println();
  Serial.println("========================================");
  Serial.println("BOOT TIMING");
  Serial.println("========================================");
  Serial.printf("Relay init       %+6lu ms\r\n", static_cast<unsigned long>(relayReadyMs - bootStartMs));
  Serial.printf("Serial init      %+6lu ms\r\n", static_cast<unsigned long>(serialReadyMs - bootStartMs));
  Serial.printf("Network init     %+6lu ms\r\n", static_cast<unsigned long>(networkReadyMs - bootStartMs));
  Serial.printf("WiFi init        %+6lu ms\r\n", static_cast<unsigned long>(wifiInitMs - bootStartMs));
  Serial.printf("Web server       %+6lu ms\r\n", static_cast<unsigned long>(webServerReadyMs - bootStartMs));
  Serial.printf("Console ready    %+6lu ms\r\n", static_cast<unsigned long>(consoleReadyMs - bootStartMs));
  Serial.printf("SETUP COMPLETE   %+6lu ms\r\n", static_cast<unsigned long>(consoleReadyMs - bootStartMs));
  Serial.printf("Report printed   %+6lu ms\r\n", static_cast<unsigned long>(now - bootStartMs));
  Serial.println("========================================");
  Serial.println("WiFi connection continues asynchronously.");
  Serial.println("========================================");
  Serial.println();
}
}

void setup() {
  bootStartMs = millis();

  Relay::begin();
  relayReadyMs = millis();

  Serial.begin(115200);
  Serial.setTxTimeoutMs(50);
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.onEvent(ARDUINO_HW_CDC_BUS_RESET_EVENT, Console::onHardwareCDCEvent);
#endif
  delay(250);
  serialReadyMs = millis();

  Serial.println();
  Serial.println("ESP32-C3 PC Relay Controller");
  Serial.println("Relay test firmware - no automatic pulses");
  Serial.println("POWER=GPIO5, RESET=GPIO6");
  Serial.print("Firmware build: ");
  Serial.println(FIRMWARE_BUILD_VERSION);

  NetConfig::begin();
  networkReadyMs = millis();

  WiFiControl::begin();
  wifiInitMs = millis();

  WebControl::begin();
  webServerReadyMs = millis();

  // The USB console is optional and can legitimately block waiting for a
  // terminal response. Do not make the boot-timing report depend on that
  // interactive prompt returning: the web server and core services are already
  // initialized here, so this is the end of the boot-critical setup sequence.
  consoleReadyMs = millis();

  Serial.println();
  Serial.println("Core services initialized; entering optional USB console.");
  printBootTiming();
  Serial.flush();

  Console::begin();
}

void loop() {
  Relay::service();
  NetConfig::service();
  WebControl::service();
  MainMenu::loop();
}
