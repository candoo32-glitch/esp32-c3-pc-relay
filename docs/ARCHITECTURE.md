# ESP32-C3 PC Relay — Architecture

## Purpose

The firmware is intentionally split by responsibility so a change to one subsystem does not require editing a monolithic `main.cpp`.

## Source layout

- `src/main.cpp` — firmware entry point only: hardware-independent startup ordering and Arduino `setup()/loop()`.
- `src/relay/` — relay GPIO initialization and safe active-low output state.
- `src/interface/` — USB Serial/JTAG console transport, terminal input framing, ANSI handling, and menu input synchronization.
- `src/network/` — persistent IPv4/DHCP/static configuration and application network settings.
- `src/wifi/` — Wi-Fi scanning, credential testing/storage, connection state, diagnostics, and ESP-IDF Wi-Fi driver interaction.
- `src/app/` — user-facing menu state machine.

## Ownership rules

1. **Relay owns GPIO5/GPIO6 initialization.** Other modules do not directly drive relay pins.
2. **Console owns terminal input framing.** Menus consume complete input transactions through `readMenuChoice()` rather than reading raw bytes themselves.
3. **NetConfig owns the `network` Preferences namespace.**
4. **WiFiControl owns the `wifi` Preferences namespace.** Application credentials are written only after a successful connection.
5. **ESP-IDF Wi-Fi driver configuration is RAM-only.** `esp_wifi_set_storage(WIFI_STORAGE_RAM)` prevents driver-side connection attempts from becoming persistent NVS configuration.
6. **MainMenu/Setup orchestrate; they do not own hardware or storage.**

## Persistent storage

There are two application-owned NVS namespaces:

- `network` — DHCP/static mode and static IPv4 parameters.
- `wifi` — SSID and password.

The ESP-IDF Wi-Fi driver's station configuration is deliberately kept in RAM. This separation is important: a failed test connection must not replace the last known-good application credentials.

## Connection transaction

A Wi-Fi connection attempt is treated as a transaction:

1. Load application credentials.
2. Prepare the STA interface.
3. Apply network configuration.
4. Apply transient station configuration.
5. Start one explicit connection attempt.
6. Collect status/events/diagnostics.
7. On success, persist application credentials.
8. On failure, disconnect and clear transient station configuration without erasing application NVS.

## Console transaction boundary

USB CDC input can arrive in separate packets. The console therefore treats CR/LF as a single line terminator, filters terminal escape sequences/control bytes, and establishes a quiet RX boundary before returning to a menu after a Wi-Fi transaction.

## Design constraint

Do not reintroduce ROM-/AP-specific hacks or hidden persistent side effects merely to make a single test pass. Changes should preserve the separation between persistent configuration, transient radio state, transport state, and UI state.
