#pragma once

#include <Arduino.h>

namespace Relay {

enum class Id : uint8_t { POWER = 0, RESET = 1 };
enum class NormalState : uint8_t { OPEN = 0, CLOSED = 1 };
enum class ActivationMode : uint8_t { LATCHED = 0, PULSE = 1 };

void begin();
void service();

bool state(Id id);
void setState(Id id, bool active);
void activate(Id id);
void deactivate(Id id);

const char* name(Id id);
bool setName(Id id, const String& value);
NormalState normalState(Id id);
bool setNormalState(Id id, NormalState value);
ActivationMode activationMode(Id id);
bool setActivationMode(Id id, ActivationMode value);
uint32_t pulseMs(Id id);
bool setPulseMs(Id id, uint32_t value);

bool powerOn();
bool resetOn();
void setPower(bool on);
void setReset(bool on);

void menu();
void printStatus();

}
