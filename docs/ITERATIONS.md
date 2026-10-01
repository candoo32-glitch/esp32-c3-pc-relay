# ESP32-C3 PC Relay — Iteration Log

This log records engineering iterations as evidence-bearing changes. A build number is not treated as proof of hardware behavior; hardware results are recorded separately from source/build results.

## 122 — Wi-Fi/NVS isolation and reset audit

**Base:** Build/commit immediately before this refactor: Wi-Fi reset/NVS audit commit `e96612a5a47af9578ef63ee4ef92cb339e6c6602`.

**Source change already completed before this documentation/refactor:**
- Set ESP-IDF Wi-Fi driver configuration storage to RAM.
- Added explicit transient station-config cleanup after failed attempts.
- Removed the destructive `WiFi.disconnect(true, false)` scan-failure path.
- Disabled automatic connect/reconnect for connection transactions.
- Corrected hostname initialization ordering.
- Reduced redundant DHCP NVS writes.

**Reason:** `esp_wifi_set_config()` normally uses persistent Wi-Fi storage. The application separately owns credentials in Preferences, so the driver needed to be isolated from NVS to preserve the application's last known-good credentials across failed tests.

**Verification state:** GitHub Actions Build 122 was queued when this record was started. Do not mark hardware behavior as verified until the board is flashed and the serial log is captured.

## 121 — Menu transaction boundary

**Goal:** prevent stale CR/LF/control bytes from a Wi-Fi transaction from becoming a command in the next menu.

**Change:** menu input was made line-transaction based and a console RX synchronization step was added after Wi-Fi attempts.

**Evidence:** Build 121 GitHub Actions run `36867686473` succeeded.

**Hardware result:** record the board result separately; source/build success does not prove the menu race is fixed under the user's terminal/USB conditions.

## 120 — Clean unpinned Wi-Fi station attempt

**Goal:** remove accidental BSSID pinning from the raw ESP-IDF station test.

**Change:** normal operation uses channel 0, `bssid_set=0`, and normal AP selection; diagnostic BSSID pinning remains disabled.

**Evidence:** Build 120 GitHub Actions run `36864093355` succeeded.

**Observed diagnostic context:** the test AP was visible at approximately -36 to -38 dBm; repeated failures reported `WIFI_REASON_AUTH_EXPIRE` (reason 2). Those observations establish the reported failure mode, not its root cause.

## 119 — Remove accidental BSSID pinning

**Goal:** stop the diagnostic path from unintentionally forcing a BSSID when normal operation was intended to be unpinned.

**Result:** source was corrected; later Build 120 was used as the clean unpinned baseline.

## 118 — Character-driven fixed menus

**Goal:** reduce menu ambiguity by restricting fixed menus to their known command set.

**Result:** later testing showed the race could still occur, so this was not treated as a final fix.

## 117 — Menu arming experiment

**Goal:** prevent stale input from immediately triggering a menu after a Wi-Fi transaction.

**Result:** the implementation discarded printable input in the arming window and was therefore not accepted as a final solution.

## 115 — Ignore empty menu lines

**Goal:** prevent CR/LF artifacts from generating an empty/unknown menu selection.

**Result:** did not fully solve the observed race.

## 114 — Raw ESP-IDF Wi-Fi path

**Goal:** establish whether Arduino's `WiFi.begin()` wrapper was involved in the authentication failure.

**Result:** the raw ESP-IDF path demonstrated that the Arduino `WiFi.begin()` wrapper was out of the connection path.

## 129 — Correct Arduino Wi-Fi RAM-storage initialization order

**Intent:** make the ESP-IDF Wi-Fi driver's RAM-only configuration policy effective before Arduino-ESP32 starts the Wi-Fi driver.

**Source evidence:** Arduino-ESP32's Wi-Fi initialization calls `esp_wifi_set_storage(WIFI_STORAGE_RAM)` when `WiFi.persistent(false)` is set before the first `WiFi.mode()` call. The previous implementation called `WiFi.mode(WIFI_STA)` first and only afterward called `esp_wifi_set_storage(WIFI_STORAGE_RAM)`, so the intended initialization ordering was not explicit or aligned with the Arduino core's documented path. citeturn2search0turn3search1

**Change:** call `WiFi.persistent(false)` before `WiFi.mode(WIFI_STA)` in `WiFiControl::begin()`, remove the late direct storage-selection call, and remove redundant `setAutoReconnect(false)` calls. Hostname initialization remains before the first Wi-Fi start.

**Hypothesis:** this removes a real initialization-order ambiguity around Wi-Fi driver NVS storage. It does not by itself establish the root cause of the historical `WIFI_REASON_AUTH_EXPIRE` failures.

**Build evidence:** GitHub Actions Build 129 was triggered for commit `ef3e13221898425c7afcbecbb782dc6f7fea8a04`; at record creation it was queued.

**Hardware evidence:** none yet.

**Next test:** flash Build 129 and capture the startup line confirming RAM-only driver configuration, then run the same Wi-Fi connection test that previously produced `AUTH_EXPIRE`. If `AUTH_EXPIRE` remains, continue at the 802.11 authentication-frame level rather than changing unrelated network settings.

## Record-keeping rule going forward

Every iteration should add a dated record before or with the source change. Each record must distinguish:

- **intent** — what we changed;
- **source evidence** — what code/build inspection proves;
- **hardware evidence** — what the board actually did;
- **hypothesis** — what is suspected but not proven;
- **next test** — the smallest test that distinguishes the remaining possibilities.

Never rewrite an old iteration to make it look successful in hindsight. Correct the record with a new dated note if later evidence changes the interpretation.
