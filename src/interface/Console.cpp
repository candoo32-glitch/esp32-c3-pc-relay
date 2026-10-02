#include <Arduino.h>

namespace Console {
bool ansiSupported = false;
volatile bool sessionLost = false;

// A CRLF is one Enter key even when CR and LF arrive in different USB
// packets. Keep this state until the next byte; do not use a short timeout.
bool consumePendingLineFeed = false;

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
void onHardwareCDCEvent(void* arg, esp_event_base_t eventBase,
                        int32_t eventId, void* eventData) {
  (void)arg;
  (void)eventData;
  if (eventBase == ARDUINO_HW_CDC_EVENTS &&
      eventId == ARDUINO_HW_CDC_BUS_RESET_EVENT) {
    // Let the main task reset the CDC transport safely.
    sessionLost = true;
  }
}
#endif

bool connected() {
  return !sessionLost && Serial.isConnected();
}

bool disconnected() {
  if (sessionLost) {
    return true;
  }

#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && \
    defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // Native USB CDC has its own bus-reset event. Do not use
  // Serial.isConnected() as a menu-input gate here: on the ESP32-C3 it can
  // transiently report false while the host terminal is still open, which
  // makes nested menus return an empty command and spin indefinitely.
  return false;
#else
  if (!Serial.isConnected()) {
    sessionLost = true;
    return true;
  }
  return false;
#endif
}

// Establish a hard RX transaction boundary before a menu is shown.
//
// A terminal CR/LF can arrive in separate USB CDC packets. If the final byte
// of the previous menu transaction arrives while Wi-Fi is connecting, the next
// menu can otherwise see that byte as its first input. The timing then appears
// random.
//
// Keep the menu closed until the USB RX queue has been quiet for a complete
// settling interval, discard everything already queued, and only then allow
// the next menu transaction to begin.
constexpr uint32_t MENU_INPUT_QUIET_MS = 250;

void prepareForMenuInput() {
  uint32_t quietSince = millis();

  while (true) {
    // This is only an RX framing barrier. Do not treat a transient USB
    // connection-state report as a command/menu failure; the actual CDC bus
    // reset handler sets sessionLost when the transport really resets.
    if (Serial.available()) {
      while (Serial.available()) {
        Serial.read();
      }
      quietSince = millis();
    }

    if (millis() - quietSince >= MENU_INPUT_QUIET_MS) {
      break;
    }

    delay(5);
  }

  consumePendingLineFeed = false;

  // One final drain handles a packet delivered exactly at the boundary.
  while (Serial.available()) {
    Serial.read();
  }
}

void resetTransport() {
  // Reinitialize native USB-Serial/JTAG after a detected disconnect/reset.
  // This clears stale RX/TX state before a new PuTTY session starts.
  Serial.end();
  delay(50);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(50);

  while (Serial.available()) {
    Serial.read();
  }

  consumePendingLineFeed = false;
  sessionLost = false;
}

void waitForConnection() {
  while (!Serial.isConnected()) {
    delay(50);
  }
  sessionLost = false;
}

void resetSession() {
  sessionLost = false;
  ansiSupported = false;
  consumePendingLineFeed = false;

  while (Serial.available()) {
    Serial.read();
  }
}

void detectANSI() {
  ansiSupported = false;
}

String readLine(bool allowEmpty = true);

void begin() {
  resetSession();
  waitForConnection();

  Serial.println();
  Serial.println("Terminal display mode");
  Serial.println("---------------------");
  Serial.println("Use ANSI colors and boxed menus?");
  Serial.println("Y = ANSI");
  Serial.println("N = Plain text");
  Serial.print("Select [Y/N]: ");

  while (!Serial.available()) {
    delay(10);
  }

  String choice = readLine();
  choice.trim();
  choice.toUpperCase();

  ansiSupported = (choice == "Y" || choice == "YES");

  Serial.println();
  Serial.print("Terminal mode: ");
  Serial.println(ansiSupported ? "ANSI" : "plain text");
}

void ansi(const char* sequence) {
  if (ansiSupported) {
    Serial.write(0x1B);
    Serial.print("[");
    Serial.print(sequence);
  }
}

void resetStyle() {
  if (ansiSupported) {
    Serial.write(0x1B);
    Serial.print("[0m");
  }
}

void clearScreen() {
  if (!ansiSupported) return;
  Serial.write(0x1B); Serial.print("[2J");
  Serial.write(0x1B); Serial.print("[H");
}

void color(const char* code) {
  if (!ansiSupported) return;
  Serial.write(0x1B); Serial.print("["); Serial.print(code);
}

String readLine(bool allowEmpty) {
  String value;
  bool inEscapeSequence = false;

  while (true) {
    if (disconnected()) {
      return "";
    }

    while (Serial.available()) {
      const uint8_t byte = static_cast<uint8_t>(Serial.read());
      const char c = static_cast<char>(byte);

      // CRLF is one Enter even when the USB CDC packet boundary splits the
      // pair. Consume only the matching LF; never treat it as a second command.
      if (consumePendingLineFeed) {
        consumePendingLineFeed = false;
        if (c == '\n') {
          continue;
        }
      }

      // Terminals can send cursor/function-key escape sequences. Do not let
      // their printable bytes ([, A, B, etc.) become menu commands.
      if (inEscapeSequence) {
        if ((byte >= 0x40 && byte <= 0x7E) || byte == 0x1B) {
          inEscapeSequence = (byte == 0x1B);
        }
        continue;
      }

      if (byte == 0x1B) {
        inEscapeSequence = true;
        continue;
      }

      if (c == '\r' || c == '\n') {
        // Some USB/terminal combinations can present a CRLF as two separate
        // line-ending events. Menu prompts must never turn an empty terminator
        // into an "Unknown selection." entry.
        consumePendingLineFeed = (c == '\r');
        Serial.println();

        if (!allowEmpty && value.isEmpty()) {
          continue;
        }

        return value;
      }

      if (c == '\b' || byte == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      // Never allow control traffic to become a command. This includes NUL,
      // CDC framing artifacts, and other non-printable bytes.
      if (byte < 0x20 || byte == 0x7F) {
        continue;
      }

      Serial.write(c);
      if (value.length() < 64) {
        value += c;
      }
    }

    delay(10);
  }
}

String readPrompt(const char* prompt) {
  Serial.print(prompt);
  return readLine();
}

String readMenuChoice(const char* prompt, const char* allowed) {
  // Every menu transition establishes a fresh RX transaction boundary before
  // printing the next prompt. This is important on native USB CDC: bytes from
  // the previous transaction can arrive after the child menu returns, and a
  // delayed CR/LF or terminal byte must never be interpreted as a new command.
  //
  // Do this here, rather than relying on individual callers, so every menu in
  // the firmware gets the same race-free input behavior.
  prepareForMenuInput();
  if (disconnected()) {
    return "";
  }

  // Fixed menus are line transactions. A command is not accepted until the
  // complete CR/LF-terminated record has arrived. This keeps a terminator from
  // becoming input to the next menu state and gives us deterministic framing.
  Serial.print(prompt);

  String line;
  bool inEscapeSequence = false;

  while (true) {
    if (disconnected()) {
      return "";
    }

    while (Serial.available()) {
      const uint8_t byte = static_cast<uint8_t>(Serial.read());

      if (consumePendingLineFeed) {
        consumePendingLineFeed = false;
        if (byte == '\n') {
          continue;
        }
      }

      if (inEscapeSequence) {
        if ((byte >= 0x40 && byte <= 0x7E) || byte == 0x1B) {
          inEscapeSequence = (byte == 0x1B);
        }
        continue;
      }

      if (byte == 0x1B) {
        inEscapeSequence = true;
        continue;
      }

      if (byte == '\r' || byte == '\n') {
        consumePendingLineFeed = (byte == '\r');

        String command = line;
        line = "";
        command.trim();
        command.toUpperCase();

        if (command.length() == 1 &&
            strchr(allowed, command[0]) != nullptr) {
          // The command character was already echoed as it was received.
          // Do not print it a second time here.

          // A USB CDC packet can contain more than one terminal transaction.
          // Do not let bytes queued behind this command become the next menu
          // command before the caller has rendered the next menu. Establish a
          // hard quiet boundary before returning the accepted command.
          prepareForMenuInput();
          return command;
        }

        // Empty/invalid records are deliberately ignored. They cannot produce
        // an "Unknown selection." state transition.
        continue;
      }

      if (byte == '\b' || byte == 127) {
        if (line.length() > 0) {
          line.remove(line.length() - 1);
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      if (byte < 0x20 || byte == 0x7F) {
        continue;
      }

      if (line.length() < 16) {
        line += static_cast<char>(byte);
        Serial.write(static_cast<char>(byte));
      }
    }

    delay(5);
  }
}

bool yesNo(const char* prompt) {
  String value = readPrompt(prompt);
  value.trim();
  value.toUpperCase();
  return value == "Y" || value == "YES";
}

String readPassword(const char* prompt) {
  Serial.print(prompt);
  String value;
  bool inEscapeSequence = false;

  while (true) {
    if (disconnected()) {
      return "";
    }

    while (Serial.available()) {
      const uint8_t byte = static_cast<uint8_t>(Serial.read());
      const char c = static_cast<char>(byte);

      if (consumePendingLineFeed) {
        consumePendingLineFeed = false;
        if (c == '\n') continue;
      }

      // Password entry gets the same terminal escape filtering as menu input.
      // Cursor/function-key sequences must never become password characters.
      if (inEscapeSequence) {
        if ((byte >= 0x40 && byte <= 0x7E) || byte == 0x1B) {
          inEscapeSequence = (byte == 0x1B);
        }
        continue;
      }

      if (byte == 0x1B) {
        inEscapeSequence = true;
        continue;
      }

      if (c == '\r' || c == '\n') {
        consumePendingLineFeed = (c == '\r');
        Serial.println();
        return value;
      }

      if (c == '\b' || byte == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);
          Serial.write('\b');
          Serial.print(' ');
          Serial.write('\b');
        }
        continue;
      }

      if (byte < 0x20 || byte == 0x7F) {
        continue;
      }

      Serial.write('*');
      if (value.length() < 64) {
        value += c;
      }
    }

    delay(10);
  }
}
}
