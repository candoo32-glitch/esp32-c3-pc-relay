#pragma once

namespace WiFiDiagnostics {
void begin();
void service();
bool enabled();
void printMenuSetting();
void toggle();
void beginConnectionAttempt();
void printLine(const char* label, const char* value,
               const char* labelColor = "1;36m",
               const char* valueColor = "1;37m");
void printText(const char* text, const char* textColor = "1;37m");
void printFailure(const char* label, const char* errorName);
}

