#include <Arduino.h>

namespace Relay {
constexpr uint8_t OFF = HIGH;
constexpr uint8_t ON  = LOW;
constexpr uint8_t POWER_RELAY = 5;
constexpr uint8_t RESET_RELAY = 6;

void begin() {
  // Relays are active-low. Set the output latch HIGH before switching the
  // GPIOs to OUTPUT so startup cannot intentionally drive a relay ON.
  digitalWrite(POWER_RELAY, OFF);
  digitalWrite(RESET_RELAY, OFF);
  pinMode(POWER_RELAY, OUTPUT);
  pinMode(RESET_RELAY, OUTPUT);
  digitalWrite(POWER_RELAY, OFF);
  digitalWrite(RESET_RELAY, OFF);
}
}
