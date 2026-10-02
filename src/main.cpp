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

void setup() {
  Relay::begin();

  Serial.begin(115200);
  Serial.setTxTimeoutMs(50);
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.onEvent(ARDUINO_HW_CDC_BUS_RESET_EVENT, Console::onHardwareCDCEvent);
#endif
  delay(250);

  Serial.println();
  Serial.println("ESP32-C3 PC Relay Controller");
  Serial.println("Relay test firmware - no automatic pulses");
  Serial.println("POWER=GPIO5, RESET=GPIO6");
  Serial.print("Firmware build: ");
  Serial.println(FIRMWARE_BUILD_VERSION);

  // WiFi startup must not depend on the USB terminal. Run the complete
  // WiFi initialization and saved-credential connection attempt first, while
  // the console remains in plain/non-ANSI mode for boot diagnostics.
  NetConfig::begin();
  WiFiControl::begin();

  // Only after WiFi has had its chance to connect do we initialize the
  // interactive terminal and ask whether ANSI rendering should be enabled.
  WebControl::begin();

  Console::begin();

  Serial.println();
  Serial.println("Serial configuration console ready.");
}

void loop() {
  Relay::service();
  NetConfig::service();
  WebControl::service();
  MainMenu::loop();
}
