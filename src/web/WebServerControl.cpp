#include <Arduino.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
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

String releaseAssetBuild(const String& url, const String& suffix) {
  const int suffixPos = url.lastIndexOf(suffix);
  if (suffixPos < 0) return "";

  int end = suffixPos;
  int start = end - 1;
  while (start >= 0 && isDigit(url[start])) --start;
  ++start;
  if (start >= end) return "";
  return url.substring(start, end);
}

String latestReleaseAssetUrl(const String& json, const String& suffix, long& version) {
  String bestUrl;
  long bestVersion = -1;
  int pos = 0;

  while ((pos = json.indexOf("\"browser_download_url\"", pos)) >= 0) {
    int valueStart = json.indexOf('"', pos + 23);
    if (valueStart < 0) break;
    ++valueStart;

    int valueEnd = valueStart;
    while (valueEnd < static_cast<int>(json.length())) {
      if (json[valueEnd] == '"' && json[valueEnd - 1] != '\\') break;
      ++valueEnd;
    }
    if (valueEnd <= valueStart) break;

    const String url = json.substring(valueStart, valueEnd);
    const bool isFirmware = suffix == ".bin" && !url.endsWith("-spiffs.bin");
    const bool isMatch = suffix == "-spiffs.bin" ? url.endsWith(suffix) : isFirmware;
    if (isMatch) {
      const String buildText = releaseAssetBuild(url, suffix);
      const long build = buildText.toInt();
      if (!buildText.isEmpty() && build > bestVersion) {
        bestVersion = build;
        bestUrl = url;
      }
    }
    pos = valueEnd + 1;
  }

  version = bestVersion;
  return bestUrl;
}

String webInterfaceBuild() {
  if (!SPIFFS.exists("/web_version.txt")) return "0";
  File file = SPIFFS.open("/web_version.txt", FILE_READ);
  if (!file) return "0";
  const String value = file.readStringUntil('\n');
  file.close();
  return value.length() > 0 ? value : "0";
}

String firmwareReleaseUrl(const String& json, long& version) {
  return latestReleaseAssetUrl(json, ".bin", version);
}

String webReleaseUrl(const String& json, long& version) {
  return latestReleaseAssetUrl(json, "-spiffs.bin", version);
}

String updatePage(const String& title, const String& body) {
  String html = "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>";
  html += htmlEscape(title);
  html += F("</h2><p>");
  html += body;
  html += F("</p><p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body></html>");
  return html;
}

bool fetchReleaseCatalog(String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const char* apiUrl = "https://api.github.com/repos/candoo32-glitch/esp32-c3-pc-relay/releases?per_page=100";
  if (!http.begin(client, apiUrl)) return false;

  http.setTimeout(15000);
  http.addHeader("User-Agent", "ESP32-C3-PC-Relay");
  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  json = http.getString();
  http.end();
  return !json.isEmpty();
}

void handleFirmwareUpdateCheck() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/html; charset=utf-8",
                updatePage("Update check unavailable", "The ESP32-C3 is not connected to Wi-Fi."));
    return;
  }

  String json;
  if (!fetchReleaseCatalog(json)) {
    server.send(502, "text/html; charset=utf-8",
                updatePage("Update check failed", "Could not retrieve the GitHub release catalog."));
    return;
  }

  const long currentFirmware = firmwareBuild().toInt();
  const long currentWeb = webInterfaceBuild().toInt();
  long latestFirmware = -1;
  long latestWeb = -1;
  const String firmwareUrl = firmwareReleaseUrl(json, latestFirmware);
  const String webUrl = webReleaseUrl(json, latestWeb);
  const bool firmwareAvailable = !firmwareUrl.isEmpty() && latestFirmware > currentFirmware;
  const bool webAvailable = !webUrl.isEmpty() && latestWeb > currentWeb;

  String html = "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><body style='font-family:system-ui;background:#111;color:#eee;padding:30px'><h2>Update check</h2>";

  if (!firmwareAvailable && !webAvailable) {
    html += F("<p>Firmware and web interface are up to date.</p>");
  } else {
    if (firmwareAvailable) {
      html += F("<p><b>Firmware update available:</b> ");
      html += String(currentFirmware);
      html += F(" → ");
      html += String(latestFirmware);
      html += F("</p>");
    } else {
      html += F("<p>Firmware is up to date (");
      html += String(currentFirmware);
      html += F(").</p>");
    }

    if (webAvailable) {
      html += F("<p><b>Web interface update available:</b> ");
      html += String(currentWeb);
      html += F(" → ");
      html += String(latestWeb);
      html += F("</p>");
    } else {
      html += F("<p>Web interface is up to date (");
      html += String(currentWeb);
      html += F(").</p>");
    }

    html += F("<p>Download and install will update only the component or components that are newer.</p>");
  }

  html += F("<p><a href='/?tab=system' style='color:#7eb6ff'>Back to System</a></p></body></html>");
  server.send(200, "text/html; charset=utf-8", html);
}

bool downloadAndWriteUpdate(HTTPClient& download, int command, const char* description) {
  const int contentLength = download.getSize();
  if (contentLength <= 0) {
    Serial.print("OTA ");
    Serial.print(description);
    Serial.println(" download did not provide a valid image size.");
    return false;
  }

  if (!Update.begin(static_cast<size_t>(contentLength), command)) {
    Serial.print("OTA ");
    Serial.print(description);
    Serial.print(" begin failed: ");
    Serial.println(Update.errorString());
    return false;
  }

  WiFiClient* stream = download.getStreamPtr();
  uint8_t* buffer = static_cast<uint8_t*>(malloc(4096));
  if (buffer == nullptr) {
    Update.abort();
    Serial.print("OTA ");
    Serial.print(description);
    Serial.println(" failed: insufficient RAM for download buffer.");
    return false;
  }

  size_t totalWritten = 0;
  bool failed = false;
  uint32_t lastYield = millis();

  // Consume the declared Content-Length rather than using connected() as
  // the loop condition. A GitHub CDN connection may close after delivering
  // the final bytes while those bytes are still buffered locally.
  while (totalWritten < static_cast<size_t>(contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!download.connected() || millis() - lastYield > 10000) {
        failed = true;
        break;
      }
      delay(1);
      yield();
      continue;
    }

    const size_t remaining = static_cast<size_t>(contentLength) - totalWritten;
    const size_t toRead = min(available, min(remaining, static_cast<size_t>(4096)));
    const int readBytes = stream->readBytes(buffer, toRead);
    if (readBytes <= 0 ||
        Update.write(buffer, static_cast<size_t>(readBytes)) != static_cast<size_t>(readBytes)) {
      failed = true;
      break;
    }

    totalWritten += static_cast<size_t>(readBytes);
    lastYield = millis();
    yield();
  }

  const bool finished =
      !failed &&
      totalWritten == static_cast<size_t>(contentLength) &&
      Update.end(true);

  free(buffer);

  if (!finished) {
    Serial.print("OTA ");
    Serial.print(description);
    Serial.print(" failed: ");
    Serial.println(Update.errorString());
    Update.abort();
    return false;
  }

  Serial.print("OTA ");
  Serial.print(description);
  Serial.print(" complete: ");
  Serial.print(totalWritten);
  Serial.println(" bytes.");
  return true;
}

void handleFirmwareUpdateLatest() {
  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/html; charset=utf-8",
                updatePage("Update unavailable", "The ESP32-C3 is not connected to Wi-Fi."));
    return;
  }

  String json;
  if (!fetchReleaseCatalog(json)) {
    server.send(502, "text/html; charset=utf-8",
                updatePage("Update failed", "Could not retrieve the GitHub release catalog."));
    return;
  }

  const long currentFirmware = firmwareBuild().toInt();
  const long currentWeb = webInterfaceBuild().toInt();
  long latestFirmware = -1;
  long latestWeb = -1;
  const String firmwareUrl = firmwareReleaseUrl(json, latestFirmware);
  const String webUrl = webReleaseUrl(json, latestWeb);
  const bool installFirmware = !firmwareUrl.isEmpty() && latestFirmware > currentFirmware;
  const bool installWeb = !webUrl.isEmpty() && latestWeb > currentWeb;

  if (!installFirmware && !installWeb) {
    server.send(200, "text/html; charset=utf-8",
                updatePage("No update installed", "Firmware and web interface are already up to date."));
    return;
  }

  WiFiClientSecure downloadClient;
  downloadClient.setInsecure();
  HTTPClient download;
  download.setTimeout(15000);
  download.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  download.addHeader("User-Agent", "ESP32-C3-PC-Relay");

  if (installFirmware) {
    if (!download.begin(downloadClient, firmwareUrl)) {
      server.send(502, "text/html; charset=utf-8",
                  updatePage("Update failed", "Could not connect to the firmware download."));
      return;
    }

    const int status = download.GET();
    if (status != HTTP_CODE_OK) {
      download.end();
      server.send(502, "text/html; charset=utf-8",
                  updatePage("Update failed", String("GitHub firmware download returned HTTP ") + String(status) + "."));
      return;
    }

    const bool finished = downloadAndWriteUpdate(download, U_FLASH, "firmware");
    download.end();
    if (!finished) {
      server.send(500, "text/html; charset=utf-8",
                  updatePage("Update failed", "The firmware image could not be downloaded or installed."));
      return;
    }
  }

  if (installWeb) {
    SPIFFS.end();

    if (!download.begin(downloadClient, webUrl)) {
      server.send(502, "text/html; charset=utf-8",
                  updatePage("Update failed", "Could not connect to the SPIFFS web interface download."));
      return;
    }

    const int status = download.GET();
    if (status != HTTP_CODE_OK) {
      download.end();
      server.send(502, "text/html; charset=utf-8",
                  updatePage("Update failed", String("GitHub SPIFFS download returned HTTP ") + String(status) + "."));
      return;
    }

    const bool finished = downloadAndWriteUpdate(download, U_SPIFFS, "SPIFFS web interface");
    download.end();
    if (!finished) {
      server.send(500, "text/html; charset=utf-8",
                  updatePage("Update failed", "The SPIFFS web interface could not be installed."));
      return;
    }
  }

  String installed;
  if (installFirmware) installed = "firmware";
  if (installWeb) {
    if (!installed.isEmpty()) installed += " and ";
    installed += "web interface";
  }

  server.send(200, "text/html; charset=utf-8",
              updatePage("Update installed", "The " + installed + " update was installed successfully. The ESP32-C3 will reboot now."));
  server.client().flush();
  delay(1000);
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

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char ch = input[i];
    switch (ch) {
      case '\\': out += F("\\\\"); break;
      case '"': out += F("\\\""); break;
      case '\b': out += F("\\b"); break;
      case '\f': out += F("\\f"); break;
      case '\n': out += F("\\n"); break;
      case '\r': out += F("\\r"); break;
      case '\t': out += F("\\t"); break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) out += ' ';
        else out += ch;
        break;
    }
  }
  return out;
}

void handlePageState() {
  const bool enabled = WiFiControl::isEnabled();
  const bool connected = enabled && WiFi.status() == WL_CONNECTED;

  String json;
  json.reserve(12000);
  json += F("{\"wifi\":{");

  auto addString = [&](const char* key, const String& value, bool comma = true) {
    json += '"';
    json += key;
    json += F("\":\"");
    json += jsonEscape(value);
    json += '"';
    if (comma) json += ',';
  };
  auto addBool = [&](const char* key, bool value, bool comma = true) {
    json += '"';
    json += key;
    json += F("\":");
    json += value ? F("true") : F("false");
    if (comma) json += ',';
  };
  auto addNumber = [&](const char* key, long long value, bool comma = true) {
    json += '"';
    json += key;
    json += F("\":");
    json += String(value);
    if (comma) json += ',';
  };

  addBool("enabled", enabled);
  addBool("connected", connected);
  addString("status", statusText());
  addString("ssid", connected ? WiFi.SSID() : "");
  addString("savedSsid", WiFiControl::savedSSID());
  addString("ip", connected ? WiFi.localIP().toString() : "");
  addString("gateway", connected ? WiFi.gatewayIP().toString() : "");
  addString("subnet", connected ? WiFi.subnetMask().toString() : "");
  addString("dns", connected ? WiFi.dnsIP().toString() : "");
  addNumber("rssi", connected ? WiFi.RSSI() : 0);
  addNumber("channel", connected ? WiFi.channel() : 0);
  addString("bssid", connected ? WiFi.BSSIDstr() : "");
  addString("auth", connected ? WiFiControl::authModeName([&]() {
    wifi_ap_record_t record = {};
    return esp_wifi_sta_get_ap_info(&record) == ESP_OK ? record.authmode : WIFI_AUTH_OPEN;
  }()) : "");
  int8_t txPower = 60;
  if (esp_wifi_get_max_tx_power(&txPower) != ESP_OK) txPower = 60;
  addString("tx", String(static_cast<float>(txPower) * 0.25f, 2), false);
  json += F("},\"network\":{");
  addString("mode", NetConfig::mode() == NetConfig::Mode::STATIC ? "static" : "dhcp");
  addString("hostname", NetConfig::hostname());
  const bool isStatic = NetConfig::mode() == NetConfig::Mode::STATIC;
  addString("ip", isStatic ? NetConfig::savedIP() : NetConfig::currentIP());
  addString("gateway", isStatic ? NetConfig::savedGateway() : NetConfig::currentGateway());
  addString("subnet", isStatic ? NetConfig::savedSubnet() : NetConfig::currentSubnet());
  addString("dns1", isStatic ? NetConfig::savedDNS1() : NetConfig::currentDNS1());
  addString("dns2", isStatic ? NetConfig::savedDNS2() : NetConfig::currentDNS2(), false);
  json += F("},\"diagnostics\":{");
  addBool("enabled", WiFiDiagnostics::enabled(), false);
  json += F("},\"relays\":[");
  for (uint8_t i = 0; i < 2; ++i) {
    const Relay::Id id = static_cast<Relay::Id>(i);
    json += '{';
    addString("name", Relay::name(id));
    addBool("state", Relay::state(id));
    addString("normal", Relay::normalState(id) == Relay::NormalState::OPEN ? "OPEN" : "CLOSED");
    addString("mode", Relay::activationMode(id) == Relay::ActivationMode::PULSE ? "PULSE" : "LATCHED");
    addNumber("pulse", Relay::pulseMs(id), false);
    json += '}';
    if (i == 0) json += ',';
  }
  json += F("],\"nvs\":{\"size\":");
  const esp_partition_t* nvs = nvsPartition();
  json += String(nvs != nullptr ? nvs->size : 0);
  json += F(",\"entries\":[");
  nvs_iterator_t iterator = nullptr;
  size_t count = 0;
  esp_err_t result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &iterator);
  while (result == ESP_OK && iterator != nullptr) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);
    if (count > 0) json += ',';
    json += F("{\"namespace\":\"");
    json += jsonEscape(info.namespace_name);
    json += F("\",\"key\":\"");
    json += jsonEscape(info.key);
    json += F("\",\"type\":\"");
    json += nvsTypeName(info.type);
    json += F("\",\"value\":\"");
    json += jsonEscape(nvsValue(info));
    json += F("\"}");
    ++count;
    result = nvs_entry_next(&iterator);
  }
  if (iterator != nullptr) nvs_release_iterator(iterator);
  json += F("]},\"system\":{");
  addString("build", firmwareBuild());
  addString("webBuild", webInterfaceBuild());
  addString("date", String(__DATE__) + F(" ") + __TIME__);
  addString("idf", esp_get_idf_version());
  addString("arduino", ESP_ARDUINO_VERSION_STR);
  addNumber("cpu", getCpuFrequencyMhz());
  addNumber("uptime", millis() / 1000UL, false);
  json += F("}}");

  server.send(200, "application/json; charset=utf-8", json);
}

void handleStaticAsset(const char* path, const char* contentType) {
  if (!SPIFFS.exists(path)) {
    server.send(404, "text/plain; charset=utf-8", "Web UI asset not found.");
    return;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain; charset=utf-8",
                "Web UI asset could not be opened.");
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void handleRoot() {
  handleStaticAsset("/index.html", "text/html; charset=utf-8");
}

void handleStyleCss() {
  handleStaticAsset("/style.css", "text/css; charset=utf-8");
}

void handleAppJs() {
  handleStaticAsset("/app.js", "application/javascript; charset=utf-8");
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
  String password = server.arg("password");
  if (password == "********") {
    WiFiControl::configureSavedCredentials(ssid);
  } else {
    WiFiControl::configureCredentials(ssid, password);
  }
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

  if (!SPIFFS.begin(false)) {
    Serial.println("Web UI filesystem mount failed; using built-in C++ UI fallback.");
  } else {
    Serial.println("Web UI filesystem mounted.");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyleCss);
  server.on("/app.js", HTTP_GET, handleAppJs);
  server.on("/api/state", HTTP_GET, handlePageState);
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