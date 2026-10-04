#include <Arduino.h>
#include <Preferences.h>
#include "Relay.h"
#include "../interface/Console.h"
#include "../diagnostics/DiagnosticsLog.h"

namespace Relay {
namespace {
constexpr uint8_t POWER_RELAY = 5;
constexpr uint8_t RESET_RELAY = 6;
constexpr uint8_t OFF = HIGH;
constexpr uint8_t ON = LOW;
constexpr uint32_t DEFAULT_PULSE_MS = 250;
constexpr uint32_t MIN_PULSE_MS = 10;
constexpr uint32_t MAX_PULSE_MS = 60000;

Preferences preferences;
struct Config {
  const char* nameKey;
  const char* normalKey;
  const char* modeKey;
  const char* pulseKey;
  uint8_t pin;
  const char* defaultName;
};
Config configs[] = {
  {"power_name", "power_normal", "power_mode", "power_pulse", POWER_RELAY, "Relay 1"},
  {"reset_name", "reset_normal", "reset_mode", "reset_pulse", RESET_RELAY, "Relay 2"}
};
uint32_t pulseDeadline[2] = {0, 0};

Config& cfg(Id id) { return configs[static_cast<uint8_t>(id)]; }
uint8_t index(Id id) { return static_cast<uint8_t>(id); }
uint8_t outputFor(Id id, bool active) {
  const bool normalClosed = normalState(id) == NormalState::CLOSED;
  const bool closed = active ? !normalClosed : normalClosed;
  return closed ? ON : OFF;
}
void writeState(Id id, bool active) { digitalWrite(cfg(id).pin, outputFor(id, active)); }
}

bool state(Id id) {
  return digitalRead(cfg(id).pin) == outputFor(id, true);
}

void setState(Id id, bool active) {
  pulseDeadline[index(id)] = 0;
  writeState(id, active);
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       (active ? " | ON" : " | OFF"));
}

void activate(Id id) {
  writeState(id, true);
  if (activationMode(id) == ActivationMode::PULSE) {
    pulseDeadline[index(id)] = millis() + pulseMs(id);
  } else {
    pulseDeadline[index(id)] = 0;
  }
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       " | ACTIVATE | " +
                       (activationMode(id) == ActivationMode::PULSE
                          ? String("PULSE=") + String(pulseMs(id)) + "ms"
                          : "LATCHED"));
}

void deactivate(Id id) {
  pulseDeadline[index(id)] = 0;
  writeState(id, false);
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) + " | DEACTIVATE");
}

const char* name(Id id) {
  static String values[2];
  const uint8_t i = index(id);
  values[i] = preferences.getString(cfg(id).nameKey, cfg(id).defaultName);
  return values[i].c_str();
}

bool setName(Id id, const String& value) {
  String trimmed = value;
  trimmed.trim();
  if (trimmed.isEmpty() || trimmed.length() > 32) return false;
  preferences.putString(cfg(id).nameKey, trimmed);
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       " | NAME=" + trimmed);
  return true;
}

NormalState normalState(Id id) {
  return preferences.getUChar(cfg(id).normalKey, static_cast<uint8_t>(NormalState::OPEN)) == static_cast<uint8_t>(NormalState::CLOSED)
           ? NormalState::CLOSED : NormalState::OPEN;
}

bool setNormalState(Id id, NormalState value) {
  preferences.putUChar(cfg(id).normalKey, static_cast<uint8_t>(value));
  // A configuration change never leaves an active relay energized.
  deactivate(id);
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       " | NORMAL=" +
                       (value == NormalState::OPEN ? "OPEN" : "CLOSED"));
  return true;
}

ActivationMode activationMode(Id id) {
  return preferences.getUChar(cfg(id).modeKey, static_cast<uint8_t>(ActivationMode::LATCHED)) == static_cast<uint8_t>(ActivationMode::PULSE)
           ? ActivationMode::PULSE : ActivationMode::LATCHED;
}

bool setActivationMode(Id id, ActivationMode value) {
  preferences.putUChar(cfg(id).modeKey, static_cast<uint8_t>(value));
  if (value == ActivationMode::LATCHED) pulseDeadline[index(id)] = 0;
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       " | MODE=" +
                       (value == ActivationMode::PULSE ? "PULSE" : "LATCHED"));
  return true;
}

uint32_t pulseMs(Id id) {
  uint32_t value = preferences.getUInt(cfg(id).pulseKey, DEFAULT_PULSE_MS);
  if (value < MIN_PULSE_MS) value = MIN_PULSE_MS;
  if (value > MAX_PULSE_MS) value = MAX_PULSE_MS;
  return value;
}

bool setPulseMs(Id id, uint32_t value) {
  if (value < MIN_PULSE_MS || value > MAX_PULSE_MS) return false;
  preferences.putUInt(cfg(id).pulseKey, value);
  DiagnosticsLog::line(String("RELAY | ") + String(index(id) + 1) +
                       " | PULSE=" + String(value) + "ms");
  return true;
}

bool powerOn() { return state(Id::POWER); }
bool resetOn() { return state(Id::RESET); }
void setPower(bool on) { setState(Id::POWER, on); }
void setReset(bool on) { setState(Id::RESET, on); }

void begin() {
  preferences.begin("relay", false);
  // Seed the relay-name NVS entries with the user-facing defaults. Migrate
  // the legacy POWER/RESET defaults, but preserve any custom names.
  if (!preferences.isKey(configs[0].nameKey) || preferences.getString(configs[0].nameKey, "") == "POWER") {
    preferences.putString(configs[0].nameKey, configs[0].defaultName);
  }
  if (!preferences.isKey(configs[1].nameKey) || preferences.getString(configs[1].nameKey, "") == "RESET") {
    preferences.putString(configs[1].nameKey, configs[1].defaultName);
  }
  for (const auto& c : configs) {
    digitalWrite(c.pin, OFF);
    pinMode(c.pin, OUTPUT);
  }
  // Defaults are inactive, so both contacts are OPEN on a fresh device.
  deactivate(Id::POWER);
  deactivate(Id::RESET);
}

void service() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; ++i) {
    if (pulseDeadline[i] != 0 && static_cast<int32_t>(now - pulseDeadline[i]) >= 0) {
      deactivate(static_cast<Id>(i));
    }
  }
}

void printStatus() {
  Serial.println();
  Serial.println("Relay status");
  Serial.println("------------");
  for (uint8_t i = 0; i < 2; ++i) {
    const Id id = static_cast<Id>(i);
    Serial.print(name(id));
    Serial.print(" (GPIO "); Serial.print(configs[i].pin); Serial.print("): ");
    Serial.println(state(id) ? "ACTIVE" : "NORMAL");
  }
}

void menu() {
  while (true) {
    Serial.println();
    Serial.println("RELAY CONTROL");
    Serial.println("=============");
    for (uint8_t i = 0; i < 2; ++i) {
      const Id id = static_cast<Id>(i);
      Serial.print(i + 1); Serial.print(". "); Serial.println(name(id));
    }
    Serial.println("3. Configure relays");
    Serial.println("4. Status");
    Serial.println("B. Back");
    String choice = Console::readMenuChoice("Select: ", "1234B");
    if (Console::disconnected()) return;
    choice.trim(); choice.toUpperCase();
    if (choice == "1" || choice == "2") {
      const Id id = choice == "1" ? Id::POWER : Id::RESET;
      Serial.print("\n"); Serial.println(name(id));
      Serial.println("A. Activate");
      Serial.println("D. Deactivate");
      String action = Console::readMenuChoice("Action: ", "ADB");
      if (Console::disconnected()) return;
      action.trim(); action.toUpperCase();
      if (action == "A") activate(id);
      else if (action == "D") deactivate(id);
    } else if (choice == "3") {
      for (uint8_t i = 0; i < 2; ++i) {
        const Id id = static_cast<Id>(i);
        Serial.println(); Serial.print("Configure "); Serial.println(name(id));
        String n = Console::readPrompt("Name [Enter=keep]: ");
        if (Console::disconnected()) return;
        if (!n.isEmpty()) setName(id, n);
        Serial.println("Normal contact: 1=OPEN, 2=CLOSED");
        String ns = Console::readMenuChoice("Select: ", "12");
        if (Console::disconnected()) return;
        if (!ns.isEmpty()) setNormalState(id, ns == "2" ? NormalState::CLOSED : NormalState::OPEN);
        Serial.println("Activation: 1=LATCHED, 2=PULSE");
        String am = Console::readMenuChoice("Select: ", "12");
        if (Console::disconnected()) return;
        if (!am.isEmpty()) setActivationMode(id, am == "2" ? ActivationMode::PULSE : ActivationMode::LATCHED);
        if (activationMode(id) == ActivationMode::PULSE) {
          String ps = Console::readPrompt("Pulse milliseconds [Enter=keep]: ");
          if (Console::disconnected()) return;
          if (!ps.isEmpty()) setPulseMs(id, static_cast<uint32_t>(ps.toInt()));
        }
      }
    } else if (choice == "4") printStatus();
    else if (choice == "B") return;
  }
}
}
