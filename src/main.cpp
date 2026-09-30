#include <Arduino.h>

namespace Pins {
constexpr uint8_t POWER_RELAY = 5; // S1
constexpr uint8_t RESET_RELAY = 6; // S2
}

namespace Relay {
constexpr uint8_t OFF = HIGH;
constexpr uint8_t ON  = LOW;

void begin() {
  // Active-low relay inputs: establish OFF before enabling outputs.
  digitalWrite(Pins::POWER_RELAY, OFF);
  digitalWrite(Pins::RESET_RELAY, OFF);
  pinMode(Pins::POWER_RELAY, OUTPUT);
  pinMode(Pins::RESET_RELAY, OUTPUT);
}

void pulse(uint8_t pin, uint32_t milliseconds = 500) {
  digitalWrite(pin, ON);
  delay(milliseconds);
  digitalWrite(pin, OFF);
}
}

void setup() {
  Relay::begin();

  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println("ESP32-C3 PC Relay Controller");
  Serial.println("Relay test firmware - no automatic pulses");
  Serial.println("POWER=GPIO5, RESET=GPIO6");
}

void loop() {
  // Safe baseline firmware.
  // Relays remain OFF until we add explicit control.
  delay(1000);
}
