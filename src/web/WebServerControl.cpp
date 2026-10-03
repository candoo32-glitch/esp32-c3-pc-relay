#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <cstring>
#include "WebServerControl.h"
#include "../wifi/WiFiControl.h"
#include "../wifi/WiFiDiagnostics.h"
#include "../network/NetConfig.h"
#include "../relay/Relay.h"

namespace WebControl {
namespace {
WebServer server(80);
bool serverStarted = false;

String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += input[i]; break;
    }
  }
  return out;
}

String statusText() {
  if (!WiFiControl::isEnabled()) return "OFF";
  if (WiFi.status() == WL_CONNECTED) return "CONNECTED";
  return "DISCONNECTED";
}

String tabName() {
  String tab = server.hasArg("tab") ? server.arg("tab") : "dashboard";
  if (tab != "dashboard" && tab != "wifi" && tab != "network" &&
      tab != "diagnostics" && tab != "relays" && tab != "storage" && tab != "system") {
    tab = "dashboard";
  }
  return tab;
}

void redirect(const char* tab) {
  String location = "/?tab=";
  location += tab;
  server.sendHeader("Location", location, true);
  server.send(303, "text/plain", "Redirecting");
}

String txPowerText() {
  int8_t txPower = 0;
  if (esp_wifi_get_max_tx_power(&txPower) != ESP_OK) return "unavailable";
  String value = String(static_cast<float>(txPower) * 0.25f, 2);
  value += F(" dBm");
  return value;
}

String firmwareBuild() {
#ifdef FW_BUILD_VERSION
  return String(FW_BUILD_VERSION);
#else
  return "0";
#endif
}

String nvsTypeName(nvs_type_t type) {
  switch (type) {
    case NVS_TYPE_U8: return "U8";
    case NVS_TYPE_I8: return "I8";
    case NVS_TYPE_U16: return "U16";
    case NVS_TYPE_I16: return "I16";
    case NVS_TYPE_U32: return "U32";
    case NVS_TYPE_I32: return "I32";
    case NVS_TYPE_U64: return "U64";
    case NVS_TYPE_I64: return "I64";
    case NVS_TYPE_STR: return "STRING";
    case NVS_TYPE_BLOB: return "BLOB";
    default: return "UNKNOWN";
  }
}

bool sensitiveNvsKey(const char* key) {
  return strcasecmp(key, "password") == 0 ||
         strcasecmp(key, "passphrase") == 0 ||
         strcasecmp(key, "token") == 0;
}

String nvsValue(const nvs_entry_info_t& entry) {
  if (sensitiveNvsKey(entry.key)) return "<hidden>";

  nvs_handle_t handle = 0;
  if (nvs_open_from_partition("nvs", entry.namespace_name,
                              NVS_READONLY, &handle) != ESP_OK) {
    return "<read error>";
  }

  String value = "<unsupported>";
  switch (entry.type) {
    case NVS_TYPE_U8: {
      uint8_t v = 0; if (nvs_get_u8(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I8: {
      int8_t v = 0; if (nvs_get_i8(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U16: {
      uint16_t v = 0; if (nvs_get_u16(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I16: {
      int16_t v = 0; if (nvs_get_i16(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U32: {
      uint32_t v = 0; if (nvs_get_u32(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_I32: {
      int32_t v = 0; if (nvs_get_i32(handle, entry.key, &v) == ESP_OK) value = String(v); break;
    }
    case NVS_TYPE_U64: {
      uint64_t v = 0; if (nvs_get_u64(handle, entry.key, &v) == ESP_OK) value = String((unsigned long long)v); break;
    }
    case NVS_TYPE_I64: {
      int64_t v = 0; if (nvs_get_i64(handle, entry.key, &v) == ESP_OK) value = String((long long)v); break;
    }
    case NVS_TYPE_STR: {
      size_t len = 0;
      if (nvs_get_str(handle, entry.key, nullptr, &len) == ESP_OK && len > 0) {
        char* buffer = new char[len];
        if (buffer != nullptr && nvs_get_str(handle, entry.key, buffer, &len) == ESP_OK) value = buffer;
        delete[] buffer;
      } else {
        value = "<empty>";
      }
      break;
    }
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if (nvs_get_blob(handle, entry.key, nullptr, &len) == ESP_OK) {
        value = "<";
        value += String(static_cast<unsigned>(len));
        value += F(" bytes>");
      }
      break;
    }
    default:
      break;
  }
  nvs_close(handle);
  return value;
}

const esp_partition_t* nvsPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  ESP_PARTITION_SUBTYPE_DATA_NVS,
                                  "nvs");
}

bool sendNvsBackup() {
  const esp_partition_t* partition = nvsPartition();
  if (partition == nullptr || partition->size == 0) {
    server.send(500, "text/plain", "NVS partition not found");
    return false;
  }

  String filename = NetConfig::hostname();
  filename.trim();
  if (filename.isEmpty()) filename = "relay";
  filename += F("-ESP32-C3-Config.backup");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.setContentLength(partition->size);
  server.send(200, "application/octet-stream", "");
  WiFiClient& client = server.client();

  uint8_t buffer[4096];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
    const size_t length = std::min(static_cast<size_t>(sizeof(buffer)), static_cast<size_t>(partition->size - offset));
    if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) return false;
    if (client.write(buffer, length) != length) return false;
    client.flush();
  }
  return true;
}

uint8_t* restoreBuffer = nullptr;
size_t restoreCapacity = 0;
size_t restoreBytes = 0;
bool restoreFailed = false;

bool firmwareUpdateFailed = false;
size_t firmwareUpdateBytes = 0;


String jsonStringField(const String& json, const char* field) {
  String needle = String("\"") + field + "\":";
  int start = json.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  while (start < static_cast<int>(json.length()) && (json[start] == ' ' || json[start] == '\t')) ++start;
  if (start >= static_cast<int>(json.length()) || json[start] != '"') return "";
  ++start;
  String value;
  while (start < static_cast<int>(json.length())) {
    char ch = json[start++];
    if (ch == '"') break;
    if (ch == '\\' && start < static_cast<int>(json.length())) {
      char escaped = json[start++];
      if (escaped == '"' || escaped == '\\' || escaped == '/') value += escaped;
      else if (escaped == 'n') value += '\n';
      else if (escaped == 'r') value += '\r';
      else if (escaped == 't') value += '\t';
      else value += escaped;
    } else {
      value += ch;
    }
  }
  return value;
}

String latestReleaseBuild(const String& tag) {
  int end = tag.length() - 1;
  while (end >= 0 && !isDigit(tag[end])) --end;
  if (end < 0) return "";
  int start = end;
  while (start > 0 && isDigit(tag[start - 1])) --start;
  return tag.substring(start, end + 1);
}

void handleFirmwareUpdateCheck() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update check unavailable</h2><p>The ESP32-C3 is not connected to Wi-Fi.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const char* apiUrl = "https://api.github.com/repos/candoo32-glitch/esp32-c3-pc-relay/releases/latest";
  if (!http.begin(client, apiUrl)) {
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update check failed</h2><p>Could not start the secure connection to GitHub.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  http.setTimeout(10000);
  http.addHeader("User-Agent", "ESP32-C3-PC-Relay");
  const int responseCode = http.GET();

  if (responseCode != HTTP_CODE_OK) {
    http.end();
    String message = "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update check failed</h2><p>GitHub returned HTTP ";
    message += String(responseCode);
    message += F(".</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    server.send(responseCode > 0 ? 502 : 504, "text/html; charset=utf-8", message);
    return;
  }

  const String json = http.getString();
  http.end();

  const String tag = jsonStringField(json, "tag_name");
  const String releaseUrl = jsonStringField(json, "html_url");
  const String latestBuild = latestReleaseBuild(tag);
  const String currentBuild = firmwareBuild();
  const long currentNumber = currentBuild.toInt();
  const long latestNumber = latestBuild.toInt();

  String html = "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'>";
  if (tag.isEmpty() || latestBuild.isEmpty()) {
    html += F("<h2>Update check failed</h2><p>GitHub returned a release response, but no firmware version could be identified.</p>");
  } else if (latestNumber > currentNumber) {
    html += F("<h2>Firmware update available</h2><p>Current firmware: <b>");
    html += htmlEscape(currentBuild);
    html += F("</b><br>Latest release: <b>");
    html += htmlEscape(tag);
    html += F("</b></p>");
    if (!releaseUrl.isEmpty()) {
      html += F("<p><a href='");
      html += htmlEscape(releaseUrl);
      html += F("' style='color:#7eb6ff'>Open GitHub release</a></p>");
    }
    html += F("<p>Download the compatible .bin from the release, then use the Firmware upgrade section to install it.</p>");
  } else {
    html += F("<h2>Firmware is up to date</h2><p>Current firmware: <b>");
    html += htmlEscape(currentBuild);
    html += F("</b><br>Latest release: <b>");
    html += htmlEscape(tag);
    html += F("</b></p>");
  }
  html += F("<p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}


String latestReleaseFirmwareUrl(const String& json) {
  int pos = 0;
  while ((pos = json.indexOf("\"browser_download_url\"", pos)) >= 0) {
    int valueStart = json.indexOf('"', pos + 23);
    if (valueStart < 0) return "";
    ++valueStart;
    int valueEnd = valueStart;
    while (valueEnd < static_cast<int>(json.length())) {
      if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') break;
      ++valueEnd;
    }
    if (valueEnd <= valueStart) return "";
    const String url = json.substring(valueStart, valueEnd);
    if (url.endsWith(".bin")) return url;
    pos = valueEnd + 1;
  }
  return "";
}

void handleFirmwareUpdateLatest() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update unavailable</h2><p>The ESP32-C3 is not connected to Wi-Fi.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const char* apiUrl = "https://api.github.com/repos/candoo32-glitch/esp32-c3-pc-relay/releases/latest";

  if (!http.begin(client, apiUrl)) {
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>Could not connect to GitHub.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  http.setTimeout(10000);
  http.addHeader("User-Agent", "ESP32-C3-PC-Relay");
  const int apiStatus = http.GET();
  if (apiStatus != HTTP_CODE_OK) {
    http.end();
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>Could not retrieve the latest GitHub release.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  const String json = http.getString();
  http.end();

  const String tag = jsonStringField(json, "tag_name");
  const String latestBuild = latestReleaseBuild(tag);
  const long currentNumber = firmwareBuild().toInt();
  const long latestNumber = latestBuild.toInt();
  if (tag.isEmpty() || latestBuild.isEmpty() || latestNumber <= currentNumber) {
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>No update installed</h2><p>The latest release is not newer than the firmware currently installed.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  const String firmwareUrl = latestReleaseFirmwareUrl(json);
  if (firmwareUrl.isEmpty()) {
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>The latest release does not contain a .bin firmware asset.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  WiFiClientSecure downloadClient;
  downloadClient.setInsecure();
  HTTPClient download;
  if (!download.begin(downloadClient, firmwareUrl)) {
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>Could not connect to the firmware download.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  download.setTimeout(15000);
  download.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  download.addHeader("User-Agent", "ESP32-C3-PC-Relay");
  const int downloadStatus = download.GET();
  if (downloadStatus != HTTP_CODE_OK) {
    download.end();
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>GitHub firmware download returned HTTP ");
    return;
  }

  const int contentLength = download.getSize();
  if (contentLength <= 0) {
    download.end();
    server.send(502, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>The firmware download did not provide a valid image size.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;  }

  if (!Update.begin(static_cast<size_t>(contentLength))) {
    download.end();
    server.send(500, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>The firmware image is too large for the OTA partition.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  WiFiClient* stream = download.getStreamPtr();
  uint8_t* buffer = static_cast<uint8_t*>(malloc(4096));
  if (buffer == nullptr) {
    download.end();
    Update.abort();
    server.send(500, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>Not enough RAM was available for the firmware download buffer.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  size_t totalWritten = 0;
  bool failed = false;
  uint32_t lastYield = millis();

  while (download.connected() && totalWritten < static_cast<size_t>(contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(1);
      if (millis() - lastYield > 10000) {
        failed = true;
        break;
      }
      continue;
    }

    const size_t toRead = min(available, sizeof(buffer));
    const int readBytes = stream->readBytes(buffer, toRead);
    if (readBytes <= 0 || Update.write(buffer, static_cast<size_t>(readBytes)) != static_cast<size_t>(readBytes)) {
      failed = true;
      break;
    }
    totalWritten += static_cast<size_t>(readBytes);
    lastYield = millis();
    yield();
  }

  const bool finished = !failed && totalWritten == static_cast<size_t>(contentLength) && Update.end(true);
  free(buffer);
  download.end();

  if (!finished) {
    Update.abort();
    server.send(500, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update failed</h2><p>The firmware download or flash operation failed. The existing firmware was not replaced.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Firmware upgraded</h2><p>The new firmware was downloaded and installed successfully. The ESP32-C3 will reboot now.</p><p><a href='/?tab=dashboard' style='color:#7eb6ff'>Return to Dashboard</a></p>");
  delay(300);
  ESP.restart();
}

void handleFirmwareUpdateUpload() {
  HTTPUpload& upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      firmwareUpdateFailed = false;
      firmwareUpdateBytes = 0;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        firmwareUpdateFailed = true;
        Serial.print("Firmware OTA begin failed: ");
        Serial.println(Update.errorString());
        break;
      }
      Serial.print("Firmware OTA started: ");
      Serial.println(upload.filename);
      break;

    case UPLOAD_FILE_WRITE:
      if (firmwareUpdateFailed) break;
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        firmwareUpdateFailed = true;
        Serial.print("Firmware OTA write failed: ");
        Serial.println(Update.errorString());
      } else {
        firmwareUpdateBytes += upload.currentSize;
      }
      break;

    case UPLOAD_FILE_END:
      if (firmwareUpdateFailed) break;
      if (!Update.end(true)) {
        firmwareUpdateFailed = true;
        Serial.print("Firmware OTA finalize failed: ");
        Serial.println(Update.errorString());
      }
      break;

    case UPLOAD_FILE_ABORTED:
      firmwareUpdateFailed = true;
      Update.abort();
      Serial.println("Firmware OTA upload aborted.");
      break;

    default:
      break;
  }
}

void handleFirmwareUpdateComplete() {
  if (firmwareUpdateFailed || firmwareUpdateBytes == 0) {
    if (Update.isRunning()) Update.abort();
    server.send(400, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Firmware upgrade failed</h2><p>The firmware image could not be uploaded or verified. The existing firmware was not replaced.</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body>");
    return;
  }

  Serial.print("Firmware OTA complete: ");
  Serial.print(firmwareUpdateBytes);
  Serial.println(" bytes. Rebooting.");
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='8;url=/?tab=system'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Firmware upgraded</h2><p>The new firmware was written successfully. The ESP32-C3 is rebooting now.</p></body>");
  delay(300);
  ESP.restart();
}

void handleConfigRestoreUpload() {
  HTTPUpload& upload = server.upload();
  const esp_partition_t* partition = nvsPartition();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      restoreBytes = 0;
      restoreFailed = false;
      if (restoreBuffer != nullptr) {
        free(restoreBuffer);
        restoreBuffer = nullptr;
      }
      restoreCapacity = partition != nullptr ? partition->size : 0;
      if (restoreCapacity == 0) {
        restoreFailed = true;
        break;
      }
      restoreBuffer = static_cast<uint8_t*>(malloc(restoreCapacity));
      if (restoreBuffer == nullptr) {
        restoreFailed = true;
        restoreCapacity = 0;
      }
      break;

    case UPLOAD_FILE_WRITE:
      if (restoreFailed || restoreBuffer == nullptr ||
          upload.currentSize > restoreCapacity - restoreBytes) {
        restoreFailed = true;
        break;
      }
      memcpy(restoreBuffer + restoreBytes, upload.buf, upload.currentSize);
      restoreBytes += upload.currentSize;
      break;

    case UPLOAD_FILE_END:
      if (restoreBuffer == nullptr || restoreBytes != restoreCapacity) restoreFailed = true;
      break;

    case UPLOAD_FILE_ABORTED:
      restoreFailed = true;
      break;

    default:
      break;
  }
}

void handleConfigRestoreComplete() {
  const esp_partition_t* partition = nvsPartition();
  const bool valid = !restoreFailed && partition != nullptr &&
                     restoreBuffer != nullptr && restoreBytes == restoreCapacity;

  if (!valid) {
    if (restoreBuffer != nullptr) free(restoreBuffer);
    restoreBuffer = nullptr;
    restoreCapacity = 0;
    restoreBytes = 0;
    server.send(400, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Restore failed</h2><p>The uploaded NVS backup was incomplete or invalid. No configuration was changed.</p><p><a href='/?tab=storage' style='color:#7eb6ff'>Back to Storage</a></p></body>");
    return;
  }

  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Configuration restored</h2><p>The NVS configuration backup was written successfully. The ESP32-C3 will reboot now.</p></body>");
  delay(300);

  esp_err_t result = esp_partition_erase_range(partition, 0, partition->size);
  if (result == ESP_OK) result = esp_partition_write(partition, 0, restoreBuffer, partition->size);

  free(restoreBuffer);
  restoreBuffer = nullptr;
  restoreCapacity = 0;
  restoreBytes = 0;

  if (result != ESP_OK) {
    Serial.print("NVS restore failed: ");
    Serial.println(esp_err_to_name(result));
  } else {
    Serial.println("NVS configuration restored from web backup.");
  }
  delay(300);
  ESP.restart();
}

String nvsTable() {
  String html;
  html.reserve(5000);
  html += F("<div class='table-wrap'><table><thead><tr><th>#</th><th>Namespace</th><th>Key</th><th>Type</th><th>Value</th></tr></thead><tbody>");

  nvs_iterator_t iterator = nullptr;
  size_t count = 0;
  esp_err_t result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &iterator);
  while (result == ESP_OK && iterator != nullptr) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);
    ++count;

    html += F("<tr><td>");
    html += String(count);
    html += F("</td><td>");
    html += htmlEscape(info.namespace_name);
    html += F("</td><td>");
    html += htmlEscape(info.key);
    html += F("</td><td>");
    html += nvsTypeName(info.type);
    html += F("</td><td>");
    html += htmlEscape(nvsValue(info));
    html += F("</td></tr>");

    result = nvs_entry_next(&iterator);
  }

  if (iterator != nullptr) nvs_release_iterator(iterator);

  html += F("</tbody></table></div><div class='muted'>");
  html += String(count);
  html += F(" entries. Password/token values are hidden.</div>");
  return html;
}

String page() {
  const String tab = tabName();
  const bool enabled = WiFiControl::isEnabled();
  const bool connected = enabled && WiFi.status() == WL_CONNECTED;
  const uint32_t uptime = millis() / 1000UL;

  String html;
  html.reserve(18000);

  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32-C3 PC Relay</title><style>");
  html += F("*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:1100px;margin:auto}");
  html += F("h1{font-size:25px;margin:0 0 4px}h2{font-size:18px;margin:0 0 14px}h3{font-size:15px;margin:20px 0 8px}.muted{color:#999}.ok{color:#62d477}.warn{color:#e5bd58}.bad{color:#ff7070}");
  html += F(".tabs{display:flex;gap:6px;overflow:auto;margin:18px 0 12px;padding-bottom:2px}.tabs a{color:#bbb;text-decoration:none;padding:10px 13px;border:1px solid #383838;border-radius:8px;white-space:nowrap;background:#191919}.tabs a.active{color:#fff;background:#303030;border-color:#666}");
  html += F(".card{background:#1c1c1c;border:1px solid #414141;border-radius:10px;padding:16px;margin:12px 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}");
  html += F("table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px 7px;border-bottom:1px solid #333;vertical-align:middle}th{color:#bbb;font-weight:600}td:first-child{color:#aaa}");
  html += F(".table-wrap{overflow:auto}.kv td:first-child{width:38%}");
  html += F("label{display:block;color:#bbb;font-size:13px;margin:0 0 5px}input,select{width:100%;font:inherit;color:#eee;background:#111;border:1px solid #555;border-radius:7px;padding:10px}input:focus,select:focus{outline:2px solid #507eb7;border-color:#507eb7}");
  html += F("input[type=number]{appearance:textfield}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:10px 0}.help{font-size:12px;color:#888;margin-top:4px}");
  html += F("button{font:inherit;padding:10px 15px;border:0;border-radius:7px;margin:5px 6px 0 0;color:white;background:#315f93;cursor:pointer}.danger{background:#7b3030}.good{background:#286a39}.secondary{background:#454545}");
  html += F(".status{display:inline-block;padding:5px 9px;border-radius:20px;background:#292929}.mono{font-family:ui-monospace,SFMono-Regular,monospace}.notice{padding:11px 13px;border:1px solid #555;border-radius:8px;background:#252525;margin:12px 0}");
  html += F(".relay-buttons{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}.relay-buttons form{margin:0}.relay-button{min-width:160px;font-size:1.05rem;font-weight:700;padding:12px 18px}.relay-float{position:fixed;right:18px;bottom:18px;z-index:1000;background:rgba(28,28,28,.96);border:1px solid #555;border-radius:12px;padding:10px 12px;box-shadow:0 8px 28px rgba(0,0,0,.45);backdrop-filter:blur(8px)}.relay-float-title{font-size:12px;color:#aaa;margin:0 0 6px;text-align:center}.relay-float-buttons{display:flex;gap:8px}.relay-float form{margin:0}.relay-float .relay-button{min-width:132px;margin:0}.firmware-update-actions{display:flex;justify-content:center;align-items:center;gap:24px;flex-wrap:wrap;margin-top:8px}.firmware-update-actions form{margin:0}</style></head><body>");

  html += F("<h1>ESP32-C3 PC Relay</h1><div class='muted'>Headless control and configuration</div>");

  html += F("<nav class='tabs'>");
  const char* names[] = {"dashboard","wifi","network","diagnostics","relays","storage","system"};
  const char* labels[] = {"Dashboard","Wi-Fi","Network","Diagnostics","Relays","Storage","System"};
  for (size_t i = 0; i < 7; ++i) {
    html += F("<a href='/?tab=");
    html += names[i];
    html += F("' class='");
    if (tab == names[i]) html += F("active");
    html += F("'>");
    html += labels[i];
    html += F("</a>");
  }
  html += F("</nav>");

  // Persistent relay controls: available from every web page without leaving the current tab.
  html += F("<div class='relay-float'><div class='relay-float-title'>Relay control</div><div class='relay-float-buttons'>");
  html += F("<form method='POST' action='/relay/action'><input type='hidden' name='id' value='0'><input type='hidden' name='return' value='");
  html += tab;
  html += F("'><button class='relay-button good' name='action' value='activate'>");
  html += htmlEscape(Relay::name(Relay::Id::POWER));
  html += F("</button></form><form method='POST' action='/relay/action'><input type='hidden' name='id' value='1'><input type='hidden' name='return' value='");
  html += tab;
  html += F("'><button class='relay-button good' name='action' value='activate'>");
  html += htmlEscape(Relay::name(Relay::Id::RESET));
  html += F("</button></form></div></div>");

  if (tab == "dashboard") {
    html += F("<div class='grid'><div class='card'><h2>Wi-Fi</h2><table class='kv'>");
    html += F("<tr><td>Status</td><td class='");
    html += connected ? F("ok'>CONNECTED") : (enabled ? F("warn'>DISCONNECTED") : F("muted'>OFF"));
    html += F("</td></tr><tr><td>SSID</td><td>");
    html += connected ? htmlEscape(WiFi.SSID()) : F("-");
    html += F("</td></tr><tr><td>IP address</td><td class='mono'>");
    html += connected ? WiFi.localIP().toString() : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr><tr><td>Channel</td><td>");
    html += connected ? String(WiFi.channel()) : F("-");
    html += F("</td></tr></table></div>");

    html += F("<div class='card'><h2>System</h2><table class='kv'><tr><td>Uptime</td><td>");
    html += String(uptime);
    html += F(" seconds</td></tr><tr><td>Firmware build</td><td>");
    html += firmwareBuild();
    html += F("</td></tr><tr><td>ESP-IDF</td><td>");
    html += esp_get_idf_version();
    html += F("</td></tr><tr><td>CPU</td><td>");
    html += String(getCpuFrequencyMhz());
    html += F(" MHz</td></tr></table></div></div>");

    html += F("<div class='card'><h2>Relay status</h2><table class='kv'><tr><td>");
    html += htmlEscape(Relay::name(Relay::Id::POWER));
    html += F("</td><td>");
    html += Relay::powerOn() ? F("<span class='ok'>ON</span>") : F("OFF");
    html += F(" &nbsp; GPIO5</td></tr><tr><td>");
    html += htmlEscape(Relay::name(Relay::Id::RESET));
    html += F("</td><td>");
    html += Relay::resetOn() ? F("<span class='ok'>ON</span>") : F("OFF");
    html += F(" &nbsp; GPIO6</td></tr></table></div>");
  }

  if (tab == "wifi") {
    html += F("<div class='card'><h2>Wi-Fi</h2><div class='status'>");
    html += statusText();
    html += F("</div><table class='kv'><tr><td>SSID</td><td>");
    html += connected ? htmlEscape(WiFi.SSID()) : F("-");
    html += F("</td></tr><tr><td>IP address</td><td class='mono'>");
    html += connected ? WiFi.localIP().toString() : F("-");
    html += F("</td></tr><tr><td>Gateway</td><td class='mono'>");
    html += connected ? WiFi.gatewayIP().toString() : F("-");
    html += F("</td></tr><tr><td>Subnet</td><td class='mono'>");
    html += connected ? WiFi.subnetMask().toString() : F("-");
    html += F("</td></tr><tr><td>DNS</td><td class='mono'>");
    html += connected ? WiFi.dnsIP().toString() : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr><tr><td>Channel</td><td>");
    html += connected ? String(WiFi.channel()) : F("-");
    html += F("</td></tr><tr><td>BSSID</td><td class='mono'>");
    html += connected ? htmlEscape(WiFi.BSSIDstr()) : F("-");
    html += F("</td></tr><tr><td>TX power</td><td>");
    html += txPowerText();
    html += F("</td></tr></table>");
    html += F("<form method='POST' action='/wifi/toggle'><button class='");
    html += enabled ? F("danger'>Turn Wi-Fi OFF") : F("good'>Turn Wi-Fi ON");
    html += F("</button></form>");
    if (enabled) html += F("<form method='POST' action='/wifi/reconnect'><button>Reconnect now</button></form>");
    html += F("</div>");

    html += F("<div class='card'><h2>Wi-Fi credentials</h2><div class='muted'>Credentials are saved only after a successful connection. SSID is limited to 32 characters and password to 63.</div>");
    html += F("<form method='POST' action='/wifi/save'><div class='row'><div><label for='ssid'>SSID</label><input id='ssid' name='ssid' maxlength='32' required value='");
    html += htmlEscape(WiFiControl::savedSSID());
    html += F("'></div><div><label for='password'>Password</label><input id='password' name='password' type='password' maxlength='63' autocomplete='new-password'><div class='help'>Leave empty only for an open network.</div></div></div><button class='good'>Test and save credentials</button></form></div>");

    html += F("<div class='card'><h2>Nearby networks</h2><form method='POST' action='/wifi/scan'><button class='secondary'>Scan now</button></form>");
    html += F("<div class='help'>A scan temporarily pauses normal Wi-Fi connection activity while results are collected.</div></div>");

    html += F("<div class='card'><h2>TX power</h2><form method='POST' action='/wifi/txpower'><div class='row'><div><label for='txpower'>Power (dBm)</label><input id='txpower' name='dbm' type='number' min='2' max='18' step='0.25' value='");
    int8_t txPower = 60;
    if (esp_wifi_get_max_tx_power(&txPower) != ESP_OK) txPower = 60;
    html += String(static_cast<float>(txPower) * 0.25f, 2);
    html += F("'><div class='help'>Allowed: 2.00–18.00 dBm in 0.25 dBm increments.</div></div></div><button>Save TX power</button></form></div>");
  }

  if (tab == "network") {
    const bool isStatic = NetConfig::mode() == NetConfig::Mode::STATIC;
    html += F("<div class='card'><h2>Network configuration</h2><form method='POST' action='/network/save'>");
    html += F("<div class='row'><div><label for='hostname'>Host name</label><input id='hostname' name='hostname' maxlength='32' pattern='[A-Za-z0-9-]+' value='");
    html += htmlEscape(NetConfig::hostname());
    html += F("'><div class='help'>Letters, numbers, and hyphens; 1–32 characters.</div></div><div><label for='mode'>Address mode</label><select id='mode' name='mode'><option value='dhcp'");
    if (!isStatic) html += F(" selected");
    html += F(">DHCP</option><option value='static'");
    if (isStatic) html += F(" selected");
    html += F(">Manual / Static IPv4</option></select></div></div>");
    const String formIP = isStatic ? NetConfig::savedIP() : NetConfig::currentIP();
    const String formGateway = isStatic ? NetConfig::savedGateway() : NetConfig::currentGateway();
    const String formSubnet = isStatic ? NetConfig::savedSubnet() : NetConfig::currentSubnet();
    const String formDNS1 = isStatic ? NetConfig::savedDNS1() : NetConfig::currentDNS1();
    const String formDNS2 = isStatic ? NetConfig::savedDNS2() : NetConfig::currentDNS2();
    html += F("<div class='row'><div><label>IP address</label><input name='ip' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(formIP);
    html += F("'><div class='help'>IPv4, four octets, maximum 255 each.</div></div><div><label>Gateway</label><input name='gateway' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(formGateway);
    html += F("'></div><div><label>Subnet mask</label><input name='subnet' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(formSubnet);
    html += F("'></div></div>");
    html += F("<div class='row'><div><label>DNS 1</label><input name='dns1' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(formDNS1);
    html += F("'></div><div><label>DNS 2</label><input name='dns2' maxlength='15' inputmode='decimal' value='");
    html += htmlEscape(formDNS2);
    html += F("'></div></div><button class='good'>Save network settings</button></form></div>");
    html += F("<div class='card'><h2>Current effective network</h2><table class='kv'><tr><td>Mode</td><td>");
    html += isStatic ? F("MANUAL / STATIC") : F("DHCP");
    html += F("</td></tr><tr><td>Hostname</td><td class='mono'>");
    html += htmlEscape(NetConfig::hostname());
    html += F(".local</td></tr><tr><td>IP</td><td class='mono'>");    html += NetConfig::currentIP();
    html += F("</td></tr><tr><td>Gateway</td><td class='mono'>");
    html += NetConfig::currentGateway();
    html += F("</td></tr><tr><td>DNS 1</td><td class='mono'>");
    html += NetConfig::currentDNS1();
    html += F("</td></tr><tr><td>DNS 2</td><td class='mono'>");
    html += NetConfig::currentDNS2();
    html += F("</td></tr></table></div>");
  }

  if (tab == "diagnostics") {
    html += F("<div class='card'><h2>Wi-Fi diagnostics</h2><table class='kv'><tr><td>Diagnostics</td><td class='");
    html += WiFiDiagnostics::enabled() ? F("ok'>ON") : F("bad'>OFF");
    html += F("</td></tr></table><form method='POST' action='/diagnostics/toggle'><button>");
    html += WiFiDiagnostics::enabled() ? F("Turn diagnostics OFF") : F("Turn diagnostics ON");
    html += F("</button></form></div>");
    html += F("<div class='card'><h2>Live connection data</h2><table class='kv'><tr><td>Wi-Fi state</td><td>");
    html += statusText();
    html += F("</td></tr><tr><td>Authentication</td><td>");
    if (connected) {
      wifi_ap_record_t record = {};
      if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) html += WiFiControl::authModeName(record.authmode);
      else html += F("unavailable");
    } else html += F("-");
    html += F("</td></tr><tr><td>BSSID</td><td class='mono'>");
    html += connected ? htmlEscape(WiFi.BSSIDstr()) : F("-");
    html += F("</td></tr><tr><td>RSSI</td><td>");
    if (connected) { html += String(WiFi.RSSI()); html += F(" dBm"); } else html += F("-");
    html += F("</td></tr></table><div class='help'>Detailed connection event records remain available on the serial diagnostics console.</div></div>");
  }

  if (tab == "relays") {
    html += F("<div class='card'><h2>Relay control</h2><div class='muted'>Each relay is independently configurable. Either Save relay settings button saves BOTH relays. Blank values use the defaults: Relay 1 / Relay 2, OPEN, LATCHED, 250 ms.</div></div>");
    html += F("");

    html += F("<form method='POST' action='/relay/config'>");
    for (uint8_t i = 0; i < 2; ++i) {
      const Relay::Id id = static_cast<Relay::Id>(i);
      const char* prefix = i == 0 ? "relay0_" : "relay1_";
      html += F("<div class='card'><h2>");
      html += htmlEscape(Relay::name(id));
      html += F("</h2><table class='kv'><tr><td>GPIO</td><td>");
      html += String(i == 0 ? 5 : 6);
      html += F("</td></tr><tr><td>Contact</td><td>");
      html += Relay::state(id) ? F("<span class='ok'>ACTIVE</span>") : F("NORMAL");
      html += F("</td></tr><tr><td>Normal state</td><td>");
      html += Relay::normalState(id) == Relay::NormalState::OPEN ? F("OPEN") : F("CLOSED");
      html += F("</td></tr><tr><td>Activation</td><td>");
      html += Relay::activationMode(id) == Relay::ActivationMode::PULSE ? F("PULSE") : F("LATCHED");
      html += F("</td></tr>");
      if (Relay::activationMode(id) == Relay::ActivationMode::PULSE) {
        html += F("<tr><td>Pulse duration</td><td>");
        html += String(Relay::pulseMs(id));
        html += F(" ms</td></tr>");
      }
      html += F("</table><div class='row'><div><label>Name</label><input name='");
      html += prefix;
      html += F("name' maxlength='32' value='");
      html += htmlEscape(Relay::name(id));
      html += F("'></div><div><label>Normal contact state</label><select name='");
      html += prefix;
      html += F("normal'><option value='open'");
      if (Relay::normalState(id) == Relay::NormalState::OPEN) html += F(" selected");
      html += F(">OPEN</option><option value='closed'");
      if (Relay::normalState(id) == Relay::NormalState::CLOSED) html += F(" selected");
      html += F(">CLOSED</option></select></div><div><label>Activation mode</label><select name='");
      html += prefix;
      html += F("mode'><option value='latched'");
      if (Relay::activationMode(id) == Relay::ActivationMode::LATCHED) html += F(" selected");
      html += F(">LATCHED</option><option value='pulse'");
      if (Relay::activationMode(id) == Relay::ActivationMode::PULSE) html += F(" selected");
      html += F(">PULSE</option></select></div><div><label>Pulse duration (ms)</label><input name='");
      html += prefix;
      html += F("pulse' type='number' min='10' max='60000' step='1' value='");
      html += String(Relay::pulseMs(id));
      html += F("'><div class='help'>10–60000 ms. Blank uses 250 ms.</div></div></div>");
      html += F("<button class='good' type='submit'>Save relay settings</button></div>");
    }
    html += F("</form>");
  }

  if (tab == "storage") {
    const esp_partition_t* nvs = nvsPartition();
    html += F("<div class='card'><h2>Configuration backup</h2>");
    html += F("<div class='muted'>Download a complete NVS configuration backup. This includes saved Wi-Fi credentials and other configuration values, so treat the backup file as sensitive.</div>");
    if (nvs != nullptr) {
      html += F("<p class='mono'>NVS partition: ");
      html += String(nvs->size);
      html += F(" bytes</p>");
    }
    html += F("<a href='/config/backup'><button class='good' type='button'>Download configuration backup</button></a></div>");
    html += F("<div class='card'><h2>Restore configuration</h2>");
    html += F("<div class='bad'>Restore replaces the entire NVS configuration and then reboots the ESP32-C3. Use a backup created by this firmware on a compatible ESP32-C3 relay device.</div>");
    html += F("<form method='POST' action='/config/restore' enctype='multipart/form-data' onsubmit='return confirm(&quot;Restore this configuration and reboot the ESP32-C3?&quot;);'><div class='row'><div><label for='configfile'>Configuration backup</label><input id='configfile' name='configfile' type='file' accept='.bin,application/octet-stream' required></div></div><button class='good'>Restore configuration and reboot</button></form></div>");
    html += F("<div class='card'><h2>NVS contents</h2>");
    html += nvsTable();
    html += F("</div><div class='card'><h2>Format NVS</h2><div class='bad'>This erases the entire NVS partition, including Wi-Fi credentials, TX power, diagnostics, and network settings.</div>");
    html += F("<form method='POST' action='/nvs/format' onsubmit='return confirm(&quot;Erase the entire NVS partition and reboot the ESP32?&quot;);'><button class='danger'>Format NVS and reboot</button></form></div>");
  }
  if (tab == "system") {
    html += F("<div class='card'><h2>Firmware</h2><table class='kv'><tr><td>Build</td><td>");
    html += firmwareBuild();
    html += F("</td></tr><tr><td>Build date</td><td>");
    html += __DATE__;
    html += F(" ");
    html += __TIME__;
    html += F("</td></tr><tr><td>ESP-IDF</td><td>");
    html += esp_get_idf_version();
    html += F("</td></tr><tr><td>Arduino core</td><td>");
    html += ESP_ARDUINO_VERSION_STR;
    html += F("</td></tr><tr><td>Chip</td><td>ESP32-C3</td></tr><tr><td>CPU frequency</td><td>");
    html += String(getCpuFrequencyMhz());
    html += F(" MHz</td></tr><tr><td>Uptime</td><td>");
    html += String(uptime);
    html += F(" seconds</td></tr></table></div>");
    html += F("<div class='card'><h2>Firmware updates</h2>");
    html += F("<div class='muted'>Check GitHub for the latest published firmware release. If a newer compatible build is available, it can be downloaded and installed directly.</div>");
    html += F("<div class='firmware-update-actions'><form method='GET' action='/system/check-update'><button class='secondary' type='submit'>Check for firmware updates</button></form>");
    html += F("<form method='POST' action='/system/update-latest' onsubmit=&quot;return confirm('Download and install the latest firmware from GitHub, then reboot the ESP32-C3?');&quot;><button class='good' type='submit'>Download and install latest firmware</button></form></div></div>");
    html += F("<div class='card'><h2>Firmware upgrade</h2>");
    html += F("<div class='warn'>Upload a compatible ESP32-C3 firmware .bin file. The current firmware will be replaced and the device will reboot automatically. NVS configuration is preserved.</div>");
    html += F("<form method='POST' action='/system/update' enctype='multipart/form-data' onsubmit='return confirm(&quot;Upgrade firmware and reboot the ESP32-C3?&quot;);'><div class='row'><div><label for='firmware'>Firmware image</label><input id='firmware' name='firmware' type='file' accept='.bin,application/octet-stream' required></div></div><button class='good' type='submit'>Upgrade firmware</button></form></div>");
    html += F("<div class='card'><h2>System actions</h2><form method='POST' action='/system/reboot' onsubmit=\"return confirm('Reboot the ESP32-C3?');\"><button class='danger'>Reboot ESP32-C3</button></form></div>");
  }

  html += F("<div class='muted'>Page status: ");
  html += statusText();
  html += F("</div></body></html>");
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", page());
}

void handleToggle() {
  const bool enable = !WiFiControl::isEnabled();
  if (!enable) {
    // Return the HTTP response before shutting down the interface so the
    // browser receives confirmation even though Wi-Fi will immediately vanish.
    server.send(200, "text/html; charset=utf-8",
                "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Wi-Fi disabled</h2><p>The ESP32-C3 Wi-Fi interface is now OFF. Re-enable it from the serial console or a future local management interface.</p></body>");
    delay(100);
    WiFiControl::setEnabled(false);
    return;
  }
  WiFiControl::setEnabled(true);
  redirect("wifi");
}

void handleReconnect() {
  WiFiControl::connect();
  redirect("wifi");
}

void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    redirect("wifi");
    return;
  }
  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  WiFiControl::configureCredentials(ssid, password);
  redirect("wifi");
}

void handleWifiScan() {
  if (!WiFiControl::isEnabled()) {
    redirect("wifi");
    return;
  }

  // Scan while remaining associated with the current access point.
  // Do not force a disconnect or change the station mode: the ESP32 can
  // perform the scan without intentionally dropping the active connection.
  WiFiControl::service();
  WiFi.scanDelete();
  const int count = WiFi.scanNetworks();

  String html;
  html.reserve(9000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Wi-Fi scan</title><style>");
  html += F("body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:1100px;margin:auto}.card{background:#1c1c1c;border:1px solid #414141;border-radius:10px;padding:16px;margin:12px 0}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px 7px;border-bottom:1px solid #333}.muted{color:#999}a,button{display:inline-block;font:inherit;padding:10px 14px;border:0;border-radius:7px;background:#315f93;color:#fff;text-decoration:none;margin-top:12px}</style></head><body>");
  html += F("<div class='card'><h2>Nearby Wi-Fi networks</h2>");

  if (count <= 0) {
    html += F("<div class='muted'>No networks found or scan failed.</div>");
  } else {
    html += F("<div style='overflow:auto'><table><thead><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Channel</th><th>Security</th><th>BSSID</th></tr></thead><tbody>");
    for (int i = 0; i < count; ++i) {
      html += F("<tr><td>");
      html += String(i + 1);
      html += F("</td><td>");
      html += WiFi.SSID(i).isEmpty() ? F("<span class='muted'>(hidden)</span>") : htmlEscape(WiFi.SSID(i));
      html += F("</td><td>");
      html += String(WiFi.RSSI(i));
      html += F(" dBm</td><td>");
      html += String(WiFi.channel(i));
      html += F("</td><td>");
      html += WiFiControl::authModeName(WiFi.encryptionType(i));
      html += F("</td><td class='mono'>");
      html += htmlEscape(WiFi.BSSIDstr(i));
      html += F("</td></tr>");
    }
    html += F("</tbody></table></div><div class='muted'>");
    html += String(count);
    html += F(" access points found.</div>");
  }

  html += F("<a href='/?tab=wifi'>Back to Wi-Fi</a></div></body></html>");
  WiFi.scanDelete();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleTxPower() {
  if (server.hasArg("dbm")) {
    const float value = server.arg("dbm").toFloat();
    WiFiControl::setTxPowerDbm(value);
  }
  redirect("wifi");
}

void handleDiagnosticsToggle() {
  const bool desired = !WiFiDiagnostics::enabled();
  if (desired != WiFiDiagnostics::enabled()) WiFiDiagnostics::toggle();
  redirect("diagnostics");
}

void handleNetworkSave() {
  if (server.hasArg("hostname")) {
    if (!NetConfig::setHostname(server.arg("hostname"))) {
      redirect("network");
      return;
    }
  }
  const String mode = server.arg("mode");
  if (mode == "dhcp") {
    NetConfig::configureDHCP();
  } else if (mode == "static" && server.hasArg("ip") && server.hasArg("gateway") &&
             server.hasArg("subnet") && server.hasArg("dns1") && server.hasArg("dns2")) {
    NetConfig::saveStatic(server.arg("ip"), server.arg("gateway"),
                          server.arg("subnet"), server.arg("dns1"),
                          server.arg("dns2"));
  }
  WiFiControl::connect();
  redirect("network");
}

void handleRelayAction() {
  if (!server.hasArg("id") || !server.hasArg("action")) {
    redirect("relays");
    return;
  }
  const int id = server.arg("id").toInt();
  if (id < 0 || id > 1) {
    redirect("relays");
    return;
  }
  const Relay::Id relay = static_cast<Relay::Id>(id);
  if (server.arg("action") == "activate") Relay::activate(relay);
  else if (server.arg("action") == "deactivate") Relay::deactivate(relay);
  String returnTab = server.hasArg("return") ? server.arg("return") : "relays";
  if (returnTab != "dashboard" && returnTab != "wifi" && returnTab != "network" &&
      returnTab != "diagnostics" && returnTab != "relays" && returnTab != "storage" && returnTab != "system") {
    returnTab = "relays";
  }
  redirect(returnTab.c_str());
}

void handleRelayConfig() {
  // Either relay's Save button submits the complete two-relay configuration.
  // Missing/blank fields intentionally fall back to the documented defaults.
  struct Defaults {
    const char* name;
    Relay::NormalState normal;
    Relay::ActivationMode mode;
    uint32_t pulse;
  };
  constexpr Defaults defaults[] = {
    {"Relay 1", Relay::NormalState::OPEN, Relay::ActivationMode::LATCHED, 250},
    {"Relay 2", Relay::NormalState::OPEN, Relay::ActivationMode::LATCHED, 250}
  };

  for (uint8_t i = 0; i < 2; ++i) {
    const Relay::Id relay = static_cast<Relay::Id>(i);
    const String prefix = i == 0 ? "relay0_" : "relay1_";

    String name = server.hasArg(prefix + "name") ? server.arg(prefix + "name") : "";
    name.trim();
    if (name.isEmpty()) name = defaults[i].name;
    if (!Relay::setName(relay, name)) {
      redirect("relays");
      return;
    }

    String normal = server.hasArg(prefix + "normal") ? server.arg(prefix + "normal") : "";
    normal.trim();
    if (normal.isEmpty()) normal = "open";
    if (normal == "open") Relay::setNormalState(relay, Relay::NormalState::OPEN);
    else if (normal == "closed") Relay::setNormalState(relay, Relay::NormalState::CLOSED);
    else {
      redirect("relays");
      return;
    }

    String mode = server.hasArg(prefix + "mode") ? server.arg(prefix + "mode") : "";
    mode.trim();
    if (mode.isEmpty()) mode = "latched";
    if (mode == "latched") Relay::setActivationMode(relay, Relay::ActivationMode::LATCHED);
    else if (mode == "pulse") Relay::setActivationMode(relay, Relay::ActivationMode::PULSE);
    else {
      redirect("relays");
      return;
    }

    String pulseText = server.hasArg(prefix + "pulse") ? server.arg(prefix + "pulse") : "";
    pulseText.trim();
    uint32_t pulse = defaults[i].pulse;
    if (!pulseText.isEmpty()) {
      const long parsed = pulseText.toInt();
      if (parsed >= 10 && parsed <= 60000) pulse = static_cast<uint32_t>(parsed);
      else {
        redirect("relays");
        return;
      }
    }
    if (!Relay::setPulseMs(relay, pulse)) {
      redirect("relays");
      return;
    }
  }

  redirect("relays");
}

void handleNvsFormat() {
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>NVS format requested</h2><p>The ESP32-C3 is erasing NVS and will reboot.</p></body>");
  delay(300);
  const esp_err_t eraseResult = nvs_flash_erase_partition("nvs");
  if (eraseResult == ESP_OK) {
    nvs_flash_init_partition("nvs");
  }
  delay(300);
  ESP.restart();
}

void handleReboot() {
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Rebooting</h2><p>The ESP32-C3 is restarting.</p></body>");
  delay(300);
  ESP.restart();
}

} // namespace

void begin() {
  if (serverStarted) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi/toggle", HTTP_POST, handleToggle);
  server.on("/wifi/reconnect", HTTP_POST, handleReconnect);
  server.on("/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/wifi/scan", HTTP_POST, handleWifiScan);
  server.on("/wifi/txpower", HTTP_POST, handleTxPower);
  server.on("/relay/action", HTTP_POST, handleRelayAction);
  server.on("/relay/config", HTTP_POST, handleRelayConfig);
  server.on("/diagnostics/toggle", HTTP_POST, handleDiagnosticsToggle);
  server.on("/network/save", HTTP_POST, handleNetworkSave);
  server.on("/config/backup", HTTP_GET, []() { sendNvsBackup(); });
  server.on("/config/restore", HTTP_POST, handleConfigRestoreComplete, handleConfigRestoreUpload);
  server.on("/nvs/format", HTTP_POST, handleNvsFormat);
  server.on("/system/update", HTTP_POST, handleFirmwareUpdateComplete, handleFirmwareUpdateUpload);
  server.on("/system/check-update", HTTP_GET, handleFirmwareUpdateCheck);
  server.on("/system/update-latest", HTTP_POST, handleFirmwareUpdateLatest);
  server.on("/system/reboot", HTTP_POST, handleReboot);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  serverStarted = true;
  Serial.println("Web server started on port 80.");
}

void service() {
  if (!serverStarted) return;
  server.handleClient();
}
}