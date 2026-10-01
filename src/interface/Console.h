#pragma once

#include <Arduino.h>

namespace Console {
extern bool ansiSupported;

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
void onHardwareCDCEvent(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData);
#endif

bool connected();
bool disconnected();
void prepareForMenuInput();
void resetTransport();
void waitForConnection();
void resetSession();
void begin();
void ansi(const char* sequence);
void resetStyle();
void clearScreen();
void color(const char* code);
String readLine(bool allowEmpty = true);
String readPrompt(const char* prompt);
String readMenuChoice(const char* prompt, const char* allowed);
bool yesNo(const char* prompt);
String readPassword(const char* prompt);
}
