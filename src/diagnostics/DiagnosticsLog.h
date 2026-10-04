#pragma once

#include <Arduino.h>

namespace DiagnosticsLog {

// Records one complete diagnostic line in the shared web/serial history.
// The line is also written to the USB serial console.
void line(const String& message);

// Adds a line to the shared history without writing it to Serial.
// Used when an existing subsystem already owns the serial formatting.
void remember(const String& message);

// Returns the most recent lines as JSON for the Diagnostics web page.
String recentJson();

}
