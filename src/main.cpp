#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

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

namespace WiFiControl {
Preferences preferences;
constexpr char PREF_NAMESPACE[] = "wifi";
constexpr char SSID_KEY[] = "ssid";
constexpr char PASSWORD_KEY[] = "password";
constexpr char HOSTNAME[] = "esp32-c3-relay";
constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;

String readLine() {
  String value;
  while (true) {
    while (Serial.available()) {
      char c = static_cast<char>(Serial.read());

      if (c == '\r') {
        continue;
      }

      if (c == '\n') {
        return value;
      }

      if (c == '\b' || c == 127) {
        if (value.length() > 0) {
          value.remove(value.length() - 1);
        }
        continue;
      }

      value += c;
    }

    delay(10);
  }
}

bool connect() {
  String ssid = preferences.getString(SSID_KEY, "");
  String password = preferences.getString(PASSWORD_KEY, "");

  if (ssid.isEmpty()) {
    Serial.println("WiFi: not configured.");
    return false;
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);

  Serial.print("WiFi: connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi: CONNECTED");
    Serial.print("WiFi SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("WiFi IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  }

  Serial.print("WiFi: connection failed, status=");
  Serial.println(static_cast<int>(WiFi.status()));
  return false;
}

void setupCredentials() {
  Serial.println();
  Serial.println("WiFi provisioning");
  Serial.println("-----------------");
  Serial.println("Enter the WiFi credentials for this device.");
  Serial.println("The password is stored in ESP32 nonvolatile storage.");
  Serial.println();

  Serial.print("SSID: ");
  String ssid = readLine();

  Serial.print("PASSWORD: ");
  String password = readLine();

  if (ssid.isEmpty()) {
    Serial.println();
    Serial.println("WiFi: SSID cannot be empty.");
    return;
  }

  preferences.putString(SSID_KEY, ssid);
  preferences.putString(PASSWORD_KEY, password);

  Serial.println();
  Serial.println("WiFi credentials saved.");
  connect();
}

void status() {
  Serial.println();
  Serial.println("WiFi status");
  Serial.println("-----------");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("State: CONNECTED");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.print("State: NOT CONNECTED (status=");
    Serial.print(static_cast<int>(WiFi.status()));
    Serial.println(")");
  }

  Serial.println();
}

void begin() {
  preferences.begin(PREF_NAMESPACE, false);

  Serial.println();
  Serial.println("WiFi subsystem starting...");
  connect();

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  SETUP  - enter WiFi credentials");
  Serial.println("  STATUS - show WiFi status");
  Serial.println();
}

void loop() {
  if (!Serial.available()) {
    return;
  }

  String command = readLine();
  command.trim();
  command.toUpperCase();

  if (command == "SETUP") {
    setupCredentials();
  } else if (command == "STATUS") {
    status();
  } else if (!command.isEmpty()) {
    Serial.println("Unknown command. Use SETUP or STATUS.");
  }
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

  WiFiControl::begin();
}

void loop() {
  // Relays remain OFF until we add explicit control.
  WiFiControl::loop();
  delay(10);
}
