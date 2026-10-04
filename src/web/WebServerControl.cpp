#include <Arduino.h>
#include <WebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
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
#include "../diagnostics/DiagnosticsLog.h"

namespace WebControl {
namespace {
// OTA simultaneous firmware + Web UI combined OTA test build marker.
WebServer server(80);
bool serverStarted = false;

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

void sendNoCache(int code) {
  addNoCacheHeaders();
  server.send(code);
}

void sendNoCache(int code, const char* contentType, const String& content) {
  addNoCacheHeaders();
  server.send(code, contentType, content);
}

String statusText() {
  if (!WiFiControl::isEnabled()) return "OFF";
  if (WiFi.status() == WL_CONNECTED) return "CONNECTED";
  return "DISCONNECTED";
}


String firmwareBuild() {
#ifdef FW_BUILD_VERSION
  return String(FW_BUILD_VERSION);
#else
  return "0";
#endif
}

constexpr const char* THEME_NVS_NAMESPACE = "web";
constexpr const char* THEME_NVS_KEY = "theme";
constexpr uint8_t DEFAULT_THEME = 0;
constexpr uint8_t THEME_COUNT = 21;

uint8_t savedTheme() {
  nvs_handle_t handle = 0;
  uint8_t value = DEFAULT_THEME;
  if (nvs_open_from_partition("nvs", THEME_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_u8(handle, THEME_NVS_KEY, &value);
    nvs_close(handle);
  }
  return value < THEME_COUNT ? value : DEFAULT_THEME;
}

bool saveTheme(uint8_t value) {
  if (value >= THEME_COUNT) return false;
  nvs_handle_t handle = 0;
  if (nvs_open_from_partition("nvs", THEME_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  const esp_err_t result = nvs_set_u8(handle, THEME_NVS_KEY, value);
  if (result == ESP_OK) nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
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
    sendNoCache(500, "text/plain", "NVS partition not found");
    return false;
  }

  String filename = NetConfig::hostname();
  filename.trim();
  if (filename.isEmpty()) filename = "relay";
  filename += F("-ESP32-C3-Config.backup");
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.setContentLength(partition->size);
  sendNoCache(200, "application/octet-stream", "");
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

bool webUpdateFailed = false;
size_t webUpdateBytes = 0;
size_t webUpdatePartitionSize = 0;

enum class OtaStage : uint8_t {
  IDLE,
  CHECKING,
  DOWNLOADING_FIRMWARE,
  WRITING_FIRMWARE,
  DOWNLOADING_WEB,
  WRITING_WEB,
  FINALIZING,
  REBOOTING,
  COMPLETE,
  ERROR
};

bool otaActive = false;
OtaStage otaStage = OtaStage::IDLE;
String otaComponent;
String otaMessage;
String otaError;
size_t otaReceived = 0;
size_t otaTotal = 0;
String otaFirmwareUrl;
String otaWebUrl;
long otaCurrentFirmwareVersion = -1;
long otaCurrentWebVersion = -1;
bool otaFirmwarePending = false;
bool otaWebPending = false;
long otaFirmwareVersion = -1;
long otaWebVersion = -1;
constexpr size_t OTA_BUFFER_SIZE = 8192;
constexpr size_t OTA_SERVICE_BYTE_BUDGET = 32768;
constexpr uint32_t OTA_SERVICE_TIME_BUDGET_MS = 12;

uint8_t* otaBuffer = nullptr;
HTTPClient otaDownload;
WiFiClientSecure otaDownloadClient;

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

String cachedWebInterfaceBuild = "0";

String webInterfaceBuild() {
  return cachedWebInterfaceBuild;
}

void loadWebInterfaceBuild() {
  cachedWebInterfaceBuild = "0";
  if (!SPIFFS.exists("/web_version.txt")) return;
  File file = SPIFFS.open("/web_version.txt", FILE_READ);
  if (!file) return;
  const String value = file.readStringUntil('\n');
  file.close();
  if (value.length() > 0) cachedWebInterfaceBuild = value;
}

String firmwareReleaseUrl(const String& json, long& version) {
  return latestReleaseAssetUrl(json, ".bin", version);
}

String webReleaseUrl(const String& json, long& version) {
  return latestReleaseAssetUrl(json, "-spiffs.bin", version);
}

bool fetchReleaseCatalog(String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const char* apiUrl = "https://api.github.com/repos/candoo32-glitch/esp32-c3-pc-relay/releases?per_page=15";
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

String jsonEscape(const String& input);

const char* otaStageName() {
  switch (otaStage) {
    case OtaStage::CHECKING: return "checking";
    case OtaStage::DOWNLOADING_FIRMWARE: return "downloading";
    case OtaStage::WRITING_FIRMWARE: return "writing";
    case OtaStage::DOWNLOADING_WEB: return "downloading";
    case OtaStage::WRITING_WEB: return "writing";
    case OtaStage::FINALIZING: return "finalizing";
    case OtaStage::REBOOTING: return "rebooting";
    case OtaStage::COMPLETE: return "complete";
    case OtaStage::ERROR: return "error";
    default: return "idle";
  }
}

void setOtaStatus(OtaStage stage, const char* component, const String& message,
                  size_t received = 0, size_t total = 0) {
  otaStage = stage;
  otaComponent = component != nullptr ? component : "";
  otaMessage = message;
  otaReceived = received;
  otaTotal = total;
}

void failOta(const String& message) {
  if (otaDownload.connected()) otaDownload.end();
  if (otaBuffer != nullptr) {
    free(otaBuffer);
    otaBuffer = nullptr;
  }
  if (Update.isRunning()) Update.abort();
  if (otaComponent == "web") SPIFFS.begin(false);
  otaError = message;
  setOtaStatus(OtaStage::ERROR, otaComponent.c_str(), message, otaReceived, otaTotal);
  otaActive = false;
  DiagnosticsLog::line(String("OTA | FAILED | ") + message);
}

void completeOta() {
  otaActive = false;
  setOtaStatus(OtaStage::COMPLETE, "", "Firmware and Web UI updates are complete.",
               otaReceived, otaTotal);
  DiagnosticsLog::line("OTA | COMPLETE | Firmware and Web UI updates are complete.");
}

bool beginOtaDownload(const String& url, const char* component, OtaStage downloadStage,
                      OtaStage writeStage) {
  setOtaStatus(downloadStage, component,
               String("Downloading ") + component + ".");
  DiagnosticsLog::line(String("OTA | START ") + component + " | " + url);

  otaDownloadClient.setInsecure();
  otaDownload.setTimeout(15000);
  otaDownload.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  otaDownload.addHeader("User-Agent", "ESP32-C3-PC-Relay");

  if (!otaDownload.begin(otaDownloadClient, url)) {
    failOta(String("Could not connect to the ") + component + " download.");
    return false;
  }

  const int responseCode = otaDownload.GET();
  if (responseCode != HTTP_CODE_OK) {
    otaDownload.end();
    failOta(String("GitHub ") + component + " download returned HTTP " +
            String(responseCode) + ".");
    return false;
  }

  const int contentLength = otaDownload.getSize();
  otaTotal = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;
  otaReceived = 0;
  setOtaStatus(writeStage, component,
               String("Writing ") + component + " as data is received.",
               0, otaTotal);
  DiagnosticsLog::line(String("OTA | WRITING ") + component +
                       " | TOTAL=" + String(otaTotal) + " bytes");

  if (!Update.begin(otaTotal > 0 ? otaTotal : UPDATE_SIZE_UNKNOWN,
                    strcmp(component, "firmware") == 0 ? U_FLASH : U_SPIFFS)) {
    const String error = Update.errorString();
    otaDownload.end();
    failOta(String("Could not start the ") + component + " update: " + error);
    return false;
  }

  if (otaBuffer == nullptr) {
    otaBuffer = static_cast<uint8_t*>(malloc(OTA_BUFFER_SIZE));
    if (otaBuffer == nullptr) {
      otaDownload.end();
      Update.abort();
      failOta(String("Insufficient RAM for the ") + component + " update buffer.");
      return false;
    }
  }

  return true;
}

bool serviceOtaWrite(const char* component) {
  if (otaBuffer == nullptr) {
    failOta(String("The ") + component + " update buffer is unavailable.");
    return false;
  }

  WiFiClient* stream = otaDownload.getStreamPtr();
  // Do not use stream->available() as the gate for OTA reads. With HTTPS/TLS,
  // data may not be buffered yet even though more data is on the way. Polling
  // available() in that case can turn a ~900 KB transfer into minutes of
  // repeated short service calls. Wait briefly for actual stream data instead.
  // Keep each network read short so the WebServer and relay control remain responsive
  // while HTTPS data is arriving. readBytes() blocks until the requested data arrives
  // or this timeout expires.
  stream->setTimeout(10);

  const uint32_t startMs = millis();
  size_t bytesThisService = 0;

  while ((otaTotal == 0 || otaReceived < otaTotal) &&
         bytesThisService < OTA_SERVICE_BYTE_BUDGET &&
         static_cast<uint32_t>(millis() - startMs) < OTA_SERVICE_TIME_BUDGET_MS) {
    const size_t remaining = otaTotal > 0 ? otaTotal - otaReceived : OTA_BUFFER_SIZE;
    const size_t toRead = min(remaining, OTA_BUFFER_SIZE);

    const size_t readBytes = stream->readBytes(otaBuffer, toRead);
    if (readBytes == 0) break;

    if (Update.write(otaBuffer, readBytes) != readBytes) {
      const String error = Update.errorString();
      otaDownload.end();
      failOta(String("Writing ") + component + " failed: " + error);
      return false;
    }

    otaReceived += readBytes;
    bytesThisService += readBytes;
  }

  if (bytesThisService > 0) {
    const size_t previousReceived = otaReceived - bytesThisService;
    if (otaTotal > 0 && ((previousReceived * 4) / otaTotal) != ((otaReceived * 4) / otaTotal)) {
      DiagnosticsLog::line(String("OTA | PROGRESS ") + component + " | " +
                           String(otaReceived) + "/" + String(otaTotal) + " bytes");
    }
    otaMessage = String("Writing ") + component + " (" +
                 String(otaReceived) +
                 (otaTotal > 0 ? String(" / ") + String(otaTotal) : String(" bytes")) +
                 ").";
  }

  if (otaTotal > 0 && otaReceived < otaTotal &&
      !otaDownload.connected() && stream->available() == 0) {
    otaDownload.end();
    failOta(String("The ") + component + " download ended before all " +
            String(otaTotal) + " bytes were received.");
    return false;
  }

  const bool transferFinished =
      otaTotal > 0 ? otaReceived >= otaTotal
                   : (!otaDownload.connected() && stream->available() == 0);

  if (!transferFinished) {
    yield();
    return true;
  }

  if (!Update.end(true)) {
    const String error = Update.errorString();
    otaDownload.end();
    failOta(String("Finalizing ") + component + " failed: " + error);
    return false;
  }

  otaDownload.end();
  free(otaBuffer);
  otaBuffer = nullptr;

  setOtaStatus(OtaStage::COMPLETE, component,
               String(component) + " update written successfully.",
               otaReceived, otaTotal);
  DiagnosticsLog::line(String("OTA | WRITTEN ") + component + " | " +
                       String(otaReceived) + "/" + String(otaTotal) + " bytes");
  return true;
}

void prepareNextOtaComponent() {
  if (otaFirmwarePending) {
    otaFirmwarePending = false;
    otaActive = true;
    if (!beginOtaDownload(otaFirmwareUrl, "firmware",
                          OtaStage::DOWNLOADING_FIRMWARE,
                          OtaStage::WRITING_FIRMWARE)) {
      return;
    }
    return;
  }

  if (otaWebPending) {
    otaWebPending = false;
    SPIFFS.end();
    otaActive = true;
    if (!beginOtaDownload(otaWebUrl, "web",
                          OtaStage::DOWNLOADING_WEB,
                          OtaStage::WRITING_WEB)) {
      SPIFFS.begin(false);
      return;
    }
    return;
  }

  setOtaStatus(OtaStage::FINALIZING, "",
               "Finalizing updates.");
  setOtaStatus(OtaStage::REBOOTING, "",
               "Updates installed successfully. Rebooting the ESP32-C3.");
  otaActive = false;
  DiagnosticsLog::line("OTA | SUCCESS | updates installed; rebooting");
  delay(1000);
  ESP.restart();
}

void serviceOta() {
  if (!otaActive) return;

  switch (otaStage) {
    case OtaStage::CHECKING: {
      if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
        failOta("The ESP32-C3 is not connected to Wi-Fi.");
        return;
      }

      String json;
      if (!fetchReleaseCatalog(json)) {
        failOta("Could not retrieve the GitHub release catalog.");
        return;
      }

      const long currentFirmware = firmwareBuild().toInt();
      const long currentWeb = webInterfaceBuild().toInt();
      otaCurrentFirmwareVersion = currentFirmware;
      otaCurrentWebVersion = currentWeb;
      otaFirmwareUrl = firmwareReleaseUrl(json, otaFirmwareVersion);
      otaWebUrl = webReleaseUrl(json, otaWebVersion);
      otaFirmwarePending = !otaFirmwareUrl.isEmpty() && otaFirmwareVersion > currentFirmware;
      otaWebPending = !otaWebUrl.isEmpty() && otaWebVersion > currentWeb;

      if (!otaFirmwarePending && !otaWebPending) {
        otaActive = false;
        DiagnosticsLog::line("OTA | UP TO DATE | firmware and Web UI");
        setOtaStatus(OtaStage::COMPLETE, "",
                     "Firmware and Web UI are already up to date.");
        return;
      }

      if (otaFirmwarePending) {
        DiagnosticsLog::line(String("OTA | AVAILABLE | firmware build ") + String(otaFirmwareVersion));
      }
      if (otaWebPending) {
        DiagnosticsLog::line(String("OTA | AVAILABLE | Web UI build ") + String(otaWebVersion));
      }

      prepareNextOtaComponent();
      return;
    }

    case OtaStage::WRITING_FIRMWARE:
      if (serviceOtaWrite("firmware")) {
        if (otaStage == OtaStage::COMPLETE) prepareNextOtaComponent();
      }
      return;

    case OtaStage::WRITING_WEB:
      if (serviceOtaWrite("web")) {
        if (otaStage == OtaStage::COMPLETE) prepareNextOtaComponent();
      }
      return;

    default:
      return;
  }
}

void handleFirmwareUpdateLatest() {
  if (otaActive || otaStage == OtaStage::REBOOTING) {
    sendNoCache(409, "application/json; charset=utf-8",
                "{\"message\":\"An OTA update is already in progress.\"}");
    return;
  }

  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    sendNoCache(503, "application/json; charset=utf-8",
                "{\"message\":\"The ESP32-C3 is not connected to Wi-Fi.\"}");
    return;
  }

  otaFirmwarePending = false;
  otaWebPending = false;
  otaFirmwareUrl = "";
  otaWebUrl = "";
  otaFirmwareVersion = -1;
  otaWebVersion = -1;
  otaCurrentFirmwareVersion = firmwareBuild().toInt();
  otaCurrentWebVersion = webInterfaceBuild().toInt();
  otaReceived = 0;
  otaTotal = 0;
  otaComponent = "";
  otaError = "";
  if (otaBuffer != nullptr) {
    free(otaBuffer);
    otaBuffer = nullptr;
  }

  otaActive = true;
  DiagnosticsLog::line("OTA | CHECK | GitHub for firmware and Web UI updates");
  setOtaStatus(OtaStage::CHECKING, "",
               "Checking GitHub for firmware and Web UI updates.");

  sendNoCache(202, "application/json; charset=utf-8",
              "{\"message\":\"OTA update started.\"}");
}

void handleFirmwareUpdateUpload() {
  HTTPUpload& upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      firmwareUpdateFailed = false;
      firmwareUpdateBytes = 0;
      String filename = upload.filename;
      filename.toLowerCase();

      if (!filename.endsWith(".bin")) {
        firmwareUpdateFailed = true;
        DiagnosticsLog::line(String("OTA | UPLOAD REJECTED | invalid firmware image name | ") + upload.filename);
        break;
      }

      DiagnosticsLog::line(String("OTA | UPLOAD START | ") + upload.filename);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        firmwareUpdateFailed = true;
        Serial.print("Firmware OTA begin failed: ");
        Serial.println(Update.errorString());
        break;
      }
      Serial.print("Firmware OTA started: ");
      Serial.println(upload.filename);
      break;
    }

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
      DiagnosticsLog::line(String("OTA | UPLOAD COMPLETE | ") + String(firmwareUpdateBytes) + " bytes");
      break;

    case UPLOAD_FILE_ABORTED:
      firmwareUpdateFailed = true;
      Update.abort();
      DiagnosticsLog::line("OTA | UPLOAD ABORTED");
      break;

    default:
      break;
  }
}

void handleFirmwareUpdateComplete(){if(firmwareUpdateFailed||firmwareUpdateBytes==0){if(Update.isRunning())Update.abort();sendNoCache(400,"application/json; charset=utf-8","{\"message\":\"The firmware image could not be uploaded or verified. The existing firmware was not replaced.\"}");return;}sendNoCache(200,"application/json; charset=utf-8","{\"message\":\"Firmware upgraded successfully. The ESP32-C3 is rebooting now.\"}");server.client().flush();delay(1000);ESP.restart();}

void handleWebFilesystemUpdateUpload() {
  HTTPUpload& upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      webUpdateFailed = false;
      webUpdateBytes = 0;
      webUpdatePartitionSize = 0;

      const esp_partition_t* partition =
          esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                   ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                   nullptr);
      String filename = upload.filename;
      filename.toLowerCase();

      if (partition == nullptr) {
        webUpdateFailed = true;
        DiagnosticsLog::line("OTA | WEB UPLOAD REJECTED | SPIFFS partition unavailable");
        break;
      }

      // Manual Web UI images are the raw SPIFFS partition image produced by
      // the build. Require the project's published -spiffs.bin naming so a
      // firmware image cannot be accidentally sent to U_SPIFFS.
      if (!filename.endsWith("-spiffs.bin")) {
        webUpdateFailed = true;
        DiagnosticsLog::line(String("OTA | WEB UPLOAD REJECTED | invalid image name | ") + upload.filename);
        break;
      }

      webUpdatePartitionSize = partition->size;
      if (!Update.begin(webUpdatePartitionSize, U_SPIFFS)) {
        webUpdateFailed = true;
        DiagnosticsLog::line(String("OTA | WEB UPLOAD REJECTED | Update.begin failed | ") +
                             Update.errorString());
        break;
      }

      DiagnosticsLog::line(String("OTA | WEB UPLOAD START | ") + upload.filename +
                           " | MAX=" + String(partition->size) + " bytes");
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (webUpdateFailed) break;
      if (webUpdateBytes > SIZE_MAX - upload.currentSize ||
          webUpdateBytes + upload.currentSize > webUpdatePartitionSize) {
        webUpdateFailed = true;
        Update.abort();
        DiagnosticsLog::line("OTA | WEB UPLOAD REJECTED | image exceeds SPIFFS partition");
        break;
      }
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        webUpdateFailed = true;
        DiagnosticsLog::line(String("OTA | WEB UPLOAD FAILED | ") + Update.errorString());
      } else {
        webUpdateBytes += upload.currentSize;
      }
      break;

    case UPLOAD_FILE_END:
      if (webUpdateFailed) break;
      if (webUpdateBytes == 0 || !Update.end(true)) {
        webUpdateFailed = true;
        if (Update.isRunning()) Update.abort();
        DiagnosticsLog::line(String("OTA | WEB UPLOAD FAILED | ") +
                             (Update.errorString()));
        break;
      }
      DiagnosticsLog::line(String("OTA | WEB UPLOAD COMPLETE | ") +
                           String(webUpdateBytes) + " bytes");
      break;

    case UPLOAD_FILE_ABORTED:
      webUpdateFailed = true;
      if (Update.isRunning()) Update.abort();
      DiagnosticsLog::line("OTA | WEB UPLOAD ABORTED");
      break;

    default:
      break;
  }
}

void handleWebFilesystemUpdateComplete() {
  if (webUpdateFailed || webUpdateBytes == 0) {
    if (Update.isRunning()) Update.abort();
    sendNoCache(400, "application/json; charset=utf-8",
                "{\"message\":\"The Web UI filesystem image could not be uploaded or verified. The existing Web UI was not replaced.\"}");
    return;
  }

  sendNoCache(200, "application/json; charset=utf-8",
              "{\"message\":\"Web UI filesystem upgraded successfully. The ESP32-C3 is rebooting now.\"}");
  server.client().flush();
  delay(1000);
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

void handleConfigRestoreComplete(){const esp_partition_t* p=nvsPartition();const bool v=!restoreFailed&&p&&restoreBuffer&&restoreBytes==restoreCapacity;if(!v){if(restoreBuffer)free(restoreBuffer);restoreBuffer=nullptr;restoreCapacity=0;restoreBytes=0;sendNoCache(400,"application/json; charset=utf-8","{\"message\":\"The uploaded NVS backup was incomplete or invalid. No configuration was changed.\"}");return;}esp_err_t r=esp_partition_erase_range(p,0,p->size);if(r==ESP_OK)r=esp_partition_write(p,0,restoreBuffer,p->size);free(restoreBuffer);restoreBuffer=nullptr;restoreCapacity=0;restoreBytes=0;if(r!=ESP_OK){sendNoCache(500,"application/json; charset=utf-8","{\"message\":\"NVS configuration restore failed.\"}");return;}sendNoCache(200,"application/json; charset=utf-8","{\"message\":\"Configuration restored successfully. The ESP32-C3 is rebooting now.\"}");server.client().flush();delay(1000);ESP.restart();}

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
  addBool("passwordSaved", WiFiControl::hasSavedPassword());
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
  json += F("},\"system\":{");

  addString("build", firmwareBuild());
  addString("webBuild", webInterfaceBuild());
  addString("date", String(__DATE__) + F(" ") + __TIME__);
  addString("idf", esp_get_idf_version());
  addString("arduino", ESP_ARDUINO_VERSION_STR);
  addNumber("cpu", getCpuFrequencyMhz());
  addNumber("uptime", millis() / 1000UL, false);
  json += F(",\"theme\":");
  json += String(savedTheme());
  json += F("}}");

  sendNoCache(200, "application/json; charset=utf-8", json);
}

void handleNvsState() {
  const esp_partition_t* nvs = nvsPartition();
  String json;
  json.reserve(8000);
  json += F("{\"size\":");
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
  json += F("]}");
  sendNoCache(200, "application/json; charset=utf-8", json);
}


void handleNvsStats() {
  const esp_partition_t* nvs = nvsPartition();
  nvs_stats_t stats{};
  const esp_err_t result = nvs_get_stats("nvs", &stats);
  if (result != ESP_OK) {
    sendNoCache(503, "application/json; charset=utf-8", "{\"message\":\"NVS statistics unavailable.\"}");
    return;
  }

  String json = F("{\"size\":");
  json += String(nvs != nullptr ? nvs->size : 0);
  json += F(",\"usedEntries\":");
  json += String(stats.used_entries);
  json += F(",\"freeEntries\":");
  json += String(stats.free_entries);
  json += F(",\"totalEntries\":");
  json += String(stats.total_entries);
  json += F(",\"namespaceCount\":");
  json += String(stats.namespace_count);
  json += F("}");
  sendNoCache(200, "application/json; charset=utf-8", json);
}


String storagePath(const String& raw) {
  String path = raw;
  path.trim();
  if (path.isEmpty()) return "";
  if (!path.startsWith("/")) path = "/" + path;
  if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0 || path.indexOf('\0') >= 0) return "";
  return path;
}

const char* storageContentType(const String& path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  if (path.endsWith(".xml")) return "application/xml; charset=utf-8";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff")) return "font/woff";
  if (path.endsWith(".woff2")) return "font/woff2";
  return "application/octet-stream";
}

void handleStorageFiles() {
  // SPIFFS is already mounted by WebControl::begin(). Do not use
  // SPIFFS.exists("/") as a mount test: "/" is the filesystem root, not a
  // regular file, so that check can report false even while the Web UI is
  // being served successfully from SPIFFS.
  String json = F("{\"total\":");
  json += String(SPIFFS.totalBytes());
  json += F(",\"used\":");
  json += String(SPIFFS.usedBytes());
  json += F(",\"files\":[");
  File root = SPIFFS.open("/");
  if (!root) {
    sendNoCache(500, "application/json; charset=utf-8", "{\"message\":\"Web storage could not be opened.\"}");
    return;
  }

  bool first = true;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      if (!first) json += ',';
      first = false;
      const String name = file.name();
      json += F("{\"path\":\"");
      json += jsonEscape(name);
      json += F("\",\"size\":");
      json += String(file.size());
      json += F("}");
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  json += F("]}");
  sendNoCache(200, "application/json; charset=utf-8", json);
}

void handleStorageView() {
  const String path = storagePath(server.arg("path"));
  if (path.isEmpty() || !SPIFFS.exists(path)) {
    sendNoCache(404, "text/plain; charset=utf-8", "File not found.");
    return;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    sendNoCache(400, "text/plain; charset=utf-8", "Not a file.");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(file, storageContentType(path));
  file.close();
}

void handleStorageDownload() {
  const String path = storagePath(server.arg("path"));
  if (path.isEmpty() || !SPIFFS.exists(path)) {
    sendNoCache(404, "text/plain; charset=utf-8", "File not found.");
    return;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    sendNoCache(400, "text/plain; charset=utf-8", "Not a file.");
    return;
  }
  String filename = path.substring(path.lastIndexOf('/') + 1);
  if (filename.isEmpty()) filename = "download";
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.streamFile(file, storageContentType(path));
  file.close();
}

void handleStorageDelete() {
  const String path = storagePath(server.arg("path"));
  if (path.isEmpty() || !SPIFFS.exists(path)) {
    sendNoCache(404, "application/json; charset=utf-8", "{\"message\":\"File not found.\"}");
    return;
  }
  if (!SPIFFS.remove(path)) {
    sendNoCache(500, "application/json; charset=utf-8", "{\"message\":\"File could not be erased.\"}");
    return;
  }
  DiagnosticsLog::line(String("WEB | STORAGE ERASE | ") + path);
  sendNoCache(200, "application/json; charset=utf-8", "{\"message\":\"File erased.\"}");
}


String storageUploadPath;
File storageUploadFile;
bool storageUploadFailed = false;
size_t storageUploadBytes = 0;

void handleStorageUpload() {
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      storageUploadFailed = false;
      storageUploadBytes = 0;
      storageUploadPath = storagePath(upload.filename);
      if (storageUploadPath.isEmpty() || storageUploadPath == "/" ||
          !storageUploadPath.substring(storageUploadPath.lastIndexOf('/') + 1).length()) {
        storageUploadFailed = true;
        break;
      }
      if (SPIFFS.exists(storageUploadPath)) SPIFFS.remove(storageUploadPath);
      storageUploadFile = SPIFFS.open(storageUploadPath, FILE_WRITE);
      if (!storageUploadFile) storageUploadFailed = true;
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (storageUploadFailed || !storageUploadFile) break;
      if (storageUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        storageUploadFailed = true;
      } else {
        storageUploadBytes += upload.currentSize;
      }
      break;
    case UPLOAD_FILE_END:
      if (storageUploadFile) storageUploadFile.close();
      if (storageUploadFailed || storageUploadBytes == 0) {
        if (storageUploadPath.length() && SPIFFS.exists(storageUploadPath)) SPIFFS.remove(storageUploadPath);
      }
      break;
    case UPLOAD_FILE_ABORTED:
      storageUploadFailed = true;
      if (storageUploadFile) storageUploadFile.close();
      if (storageUploadPath.length() && SPIFFS.exists(storageUploadPath)) SPIFFS.remove(storageUploadPath);
      break;
    default:
      break;
  }
}

void handleStorageUploadComplete() {
  if (storageUploadFailed || storageUploadPath.isEmpty() || storageUploadBytes == 0) {
    sendNoCache(400, "application/json; charset=utf-8", "{\"message\":\"The Web UI file could not be uploaded.\"}");
    return;
  }
  DiagnosticsLog::line(String("WEB | STORAGE UPLOAD | ") + storageUploadPath + " | " + String(storageUploadBytes) + " bytes");
  sendNoCache(200, "application/json; charset=utf-8", "{\"message\":\"File uploaded.\"}");
}

const char RECOVERY_PAGE[] PROGMEM = R"RECOVERY(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>PC Relay Recovery</title>
<style>
html,body{margin:0;background:#0d0f14;color:#eee;font-family:Arial,sans-serif}
body{max-width:760px;margin:0 auto;padding:24px;box-sizing:border-box}
h1{font-size:25px;margin:0 0 7px}
h2{font-size:15px;margin:0 0 10px}
p{line-height:1.45;color:#b9c0cc}
.card{background:#171a21;border:1px solid #363c47;border-radius:9px;padding:16px;margin:13px 0}
button{width:100%;padding:16px;background:#315f93;color:#fff;border:0;border-radius:6px;font-size:17px;font-weight:bold}
button:disabled{opacity:.5}
.status{margin-top:13px;padding:12px;border-radius:6px;background:#20242c;border:1px solid #3a414d;font-weight:bold}
.status.ok{border-color:#286a39;color:#8be09a}.status.warn{border-color:#8b6a24;color:#ffd36a}.status.bad{border-color:#7b3030;color:#ff9b9b}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.stat{background:#11141a;border:1px solid #303641;border-radius:7px;padding:10px}
.label{font-size:10px;text-transform:uppercase;letter-spacing:.08em;color:#8f98a8}.value{margin-top:4px;font-weight:bold;overflow-wrap:anywhere}
#diagnostics{height:260px;box-sizing:border-box;width:100%;resize:vertical;background:#090b0f;color:#b9f5c2;border:1px solid #3a414d;border-radius:6px;padding:12px;font:12px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace;white-space:pre;overflow:auto}
.small{font-size:12px;color:#8f98a8}
@media(max-width:600px){body{padding:16px}.grid{grid-template-columns:repeat(2,1fr)}}
</style>
</head>
<body>
<h1>PC Relay Recovery</h1>
<p>This page is independent of the normal Web UI. It can update the firmware and Web UI directly from GitHub when the normal interface is unavailable.</p>
<div class="card">
<button id="update" type="button">UPDATE FROM GITHUB</button>
<div id="status" class="status">Ready — waiting for an update request.</div>
</div>
<div class="card">
<h2>OTA status</h2>
<div class="grid">
<div class="stat"><div class="label">Stage</div><div id="stage" class="value">IDLE</div></div>
<div class="stat"><div class="label">Component</div><div id="component" class="value">-</div></div>
<div class="stat"><div class="label">Firmware</div><div id="firmware" class="value">-</div></div>
<div class="stat"><div class="label">Web UI</div><div id="web" class="value">-</div></div>
</div>
<div class="small" id="progress" style="margin-top:10px">No OTA activity.</div>
</div>
<div class="card">
<h2>OTA diagnostics</h2>
<div class="small">Live OTA-related messages retained by the ESP32. This helps distinguish an active update from a version conflict, GitHub lookup failure, download failure, or other stop.</div>
<textarea id="diagnostics" readonly spellcheck="false"></textarea>
</div>
<script>
const $=id=>document.getElementById(id);
let running=false;
let nearBottom=true;
function setStatus(message,kind){
 const e=$("status");e.textContent=message;e.className="status "+(kind||"");
}
function versionText(current,latest){
 if(current<0)return "-";
 if(latest<0)return "Build "+current+" / latest unavailable";
 if(latest>current)return "Build "+current+" → "+latest+" (update)";
 if(latest===current)return "Build "+current+" (current)";
 return "Build "+current+" → "+latest;
}
function render(d){
 $("stage").textContent=d.stage||"IDLE";
 $("component").textContent=d.component||"-";
 $("firmware").textContent=versionText(d.currentFirmware,d.latestFirmware);
 $("web").textContent=versionText(d.currentWeb,d.latestWeb);
 if(d.total>0){
   const p=Math.min(100,Math.max(0,(d.received/d.total)*100));
   $("progress").textContent=(d.component||"OTA")+" — "+d.received.toLocaleString()+" / "+d.total.toLocaleString()+" bytes ("+Math.round(p)+"%)";
 }else $("progress").textContent=d.message||"No OTA activity.";
 const lines=(d.diagnostics&&d.diagnostics.lines)||[];
 const area=$("diagnostics");
 nearBottom=area.scrollHeight-area.scrollTop-area.clientHeight<40;
 area.value=lines.join("\n");
 if(nearBottom)area.scrollTop=area.scrollHeight;
 if(d.stage==="error")setStatus(d.error||d.message||"OTA update failed.","bad");
 else if(d.stage==="rebooting")setStatus(d.message||"Update complete. Waiting for reboot…","ok");
 else if(d.active)setStatus(d.message||"OTA update is active…","warn");
 else if(d.stage==="complete")setStatus(d.message||"Update complete.","ok");
 else if(d.stage==="idle"&&running)setStatus("OTA is no longer active. The device may be rebooting or the update stopped.","warn");
}
async function poll(){
 try{
   const r=await fetch("/recovery/status",{cache:"no-store"});
   if(!r.ok)throw Error(r.status);
   render(await r.json());
 }catch(e){
   if(running)setStatus("Connection lost — the ESP32 may be rebooting. Waiting…","warn");
 }
}
$("update").onclick=async function(){
 if(running)return;
 running=true;this.disabled=true;
 setStatus("Starting OTA…","warn");
 try{
   const r=await fetch("/recovery/update",{method:"POST",cache:"no-store"});
   let d={};try{d=await r.json()}catch(e){}
   if(!r.ok)throw Error(d.message||"OTA request failed (HTTP "+r.status+").");
   await poll();
 }catch(e){
   setStatus(e.message||"Could not start OTA.","bad");
   this.disabled=false;running=false;
 }
};
poll();
setInterval(poll,500);
</script>
</body>
</html>
)RECOVERY";

void handleStaticAsset(const char* path, const char* contentType) {
  if (!SPIFFS.exists(path)) {
    sendNoCache(404, "text/plain; charset=utf-8", "Web UI asset not found.");
    return;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    sendNoCache(500, "text/plain; charset=utf-8",
                "Web UI asset could not be opened.");
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void handleRecoveryPage() {
  sendNoCache(200, "text/html; charset=utf-8", RECOVERY_PAGE);
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

void handleToggle(){const bool enable=!WiFiControl::isEnabled();
  DiagnosticsLog::line(String("WEB | WIFI TOGGLE | ") + (enable ? "ON" : "OFF"));if(!enable){sendNoCache(204);delay(100);WiFiControl::setEnabled(false);return;}WiFiControl::setEnabled(true);sendNoCache(204);}

void handleReconnect() {
  DiagnosticsLog::line("WEB | WIFI RECONNECT");
  WiFiControl::connect();
  sendNoCache(204);
}

void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    sendNoCache(204);
    return;
  }
  const String ssid = server.arg("ssid");
  String password = server.arg("password");
  DiagnosticsLog::line(String("WEB | WIFI SAVE | SSID=") + ssid);
  if (password == "********") {
    WiFiControl::configureSavedCredentials(ssid);
  } else {
    WiFiControl::configureCredentials(ssid, password);
  }
  sendNoCache(204);
}

void handleWifiScan(){DiagnosticsLog::line("WEB | WIFI SCAN");if(!WiFiControl::isEnabled()){sendNoCache(409,"application/json; charset=utf-8","{\"networks\":[]}");return;}WiFiControl::service();WiFi.scanDelete();const int count=WiFi.scanNetworks();String j=F("{\"networks\":[");for(int n=0;n<count;++n){if(n)j+=',';j+=F("{\"ssid\":\"");j+=jsonEscape(WiFi.SSID(n));j+=F("\",\"rssi\":");j+=String(WiFi.RSSI(n));j+=F(",\"channel\":");j+=String(WiFi.channel(n));j+=F(",\"security\":\"");j+=jsonEscape(WiFiControl::authModeName(WiFi.encryptionType(n)));j+=F("\",\"bssid\":\"");j+=jsonEscape(WiFi.BSSIDstr(n));j+=F("\"}");}j+=F("]}");WiFi.scanDelete();sendNoCache(200,"application/json; charset=utf-8",j);}

void handleTxPower() {
  if (server.hasArg("dbm")) {
    DiagnosticsLog::line(String("WEB | WIFI TX POWER | ") + server.arg("dbm") + " dBm");
    const float value = server.arg("dbm").toFloat();
    WiFiControl::setTxPowerDbm(value);
  }
  sendNoCache(204);
}

void handleDiagnosticsToggle() {
  const bool desired = !WiFiDiagnostics::enabled();
  DiagnosticsLog::line(String("WEB | WIFI DIAGNOSTICS | ") + (desired ? "ON" : "OFF"));
  if (desired != WiFiDiagnostics::enabled()) WiFiDiagnostics::toggle();
  sendNoCache(204);
}

void handleNetworkSave() {
  DiagnosticsLog::line("WEB | NETWORK SAVE");
  if (server.hasArg("hostname")) {
    if (!NetConfig::setHostname(server.arg("hostname"))) {
      sendNoCache(204);
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
  sendNoCache(204);
}

void handleRelayAction() {
  DiagnosticsLog::line(String("WEB | RELAY ACTION | id=") + server.arg("id") + " action=" + server.arg("action"));
  if (!server.hasArg("id") || !server.hasArg("action")) {
    sendNoCache(204);
    return;
  }
  const int id = server.arg("id").toInt();
  if (id < 0 || id > 1) {
    sendNoCache(204);
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
  sendNoCache(204);
}

void handleRelayConfig() {
  DiagnosticsLog::line("WEB | RELAY CONFIG SAVE");
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
      sendNoCache(204);
      return;
    }

    String normal = server.hasArg(prefix + "normal") ? server.arg(prefix + "normal") : "";
    normal.trim();
    if (normal.isEmpty()) normal = "open";
    if (normal == "open") Relay::setNormalState(relay, Relay::NormalState::OPEN);
    else if (normal == "closed") Relay::setNormalState(relay, Relay::NormalState::CLOSED);
    else {
      sendNoCache(204);
      return;
    }

    String mode = server.hasArg(prefix + "mode") ? server.arg(prefix + "mode") : "";
    mode.trim();
    if (mode.isEmpty()) mode = "latched";
    if (mode == "latched") Relay::setActivationMode(relay, Relay::ActivationMode::LATCHED);
    else if (mode == "pulse") Relay::setActivationMode(relay, Relay::ActivationMode::PULSE);
    else {
      sendNoCache(204);
      return;
    }

    String pulseText = server.hasArg(prefix + "pulse") ? server.arg(prefix + "pulse") : "";
    pulseText.trim();
    uint32_t pulse = defaults[i].pulse;
    if (!pulseText.isEmpty()) {
      const long parsed = pulseText.toInt();
      if (parsed >= 10 && parsed <= 60000) pulse = static_cast<uint32_t>(parsed);
      else {
        sendNoCache(204);
        return;
      }
    }
    if (!Relay::setPulseMs(relay, pulse)) {
      sendNoCache(204);
      return;
    }
  }

  sendNoCache(204);
}

void handleNvsFormat(){sendNoCache(204);delay(300);const esp_err_t r=nvs_flash_erase_partition("nvs");if(r==ESP_OK)nvs_flash_init_partition("nvs");delay(300);ESP.restart();}

void handleReboot(){sendNoCache(204);delay(300);ESP.restart();}

} // namespace

void begin() {
  if (serverStarted) return;

  if (!SPIFFS.begin(false)) {
    DiagnosticsLog::line("WEB | SPIFFS MOUNT FAILED");
    Serial.println("Web UI filesystem mount failed.");
  } else {
    loadWebInterfaceBuild();
    DiagnosticsLog::line(String("WEB | SPIFFS MOUNTED | build=") + cachedWebInterfaceBuild);
    Serial.println("Web UI filesystem mounted.");
  }

  server.on("/recovery", HTTP_GET, handleRecoveryPage);
  server.on("/recovery/status", HTTP_GET, []() {
    String json = F("{\"active\":");
    json += otaActive ? F("true") : F("false");
    json += F(",\"stage\":\"");
    json += jsonEscape(otaStageName());
    json += F("\",\"component\":\"");
    json += jsonEscape(otaComponent);
    json += F("\",\"currentFirmware\":");
    json += String(otaCurrentFirmwareVersion);
    json += F(",\"latestFirmware\":");
    json += String(otaFirmwareVersion);
    json += F(",\"currentWeb\":");
    json += String(otaCurrentWebVersion);
    json += F(",\"latestWeb\":");
    json += String(otaWebVersion);
    json += F(",\"received\":");
    json += String(otaReceived);
    json += F(",\"total\":");
    json += String(otaTotal);
    json += F(",\"message\":\"");
    json += jsonEscape(otaMessage);
    json += F("\",\"error\":\"");
    json += jsonEscape(otaError);
    json += F("\",\"diagnostics\":");
    json += DiagnosticsLog::recentOtaJson();
    json += '}';
    server.sendHeader("Cache-Control", "no-store");
    sendNoCache(200, "application/json; charset=utf-8", json);
  });
  server.on("/recovery/update", HTTP_POST, handleFirmwareUpdateLatest);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyleCss);
  server.on("/app.js", HTTP_GET, handleAppJs);
  server.on("/api/state", HTTP_GET, handlePageState);
  server.on("/api/nvs", HTTP_GET, handleNvsState);
  server.on("/api/nvs/stats", HTTP_GET, handleNvsStats);
  server.on("/api/storage/files", HTTP_GET, handleStorageFiles);
  server.on("/storage/view", HTTP_GET, handleStorageView);
  server.on("/storage/download", HTTP_GET, handleStorageDownload);
  server.on("/storage/delete", HTTP_POST, handleStorageDelete);
  server.on("/storage/upload", HTTP_POST, handleStorageUploadComplete, handleStorageUpload);
  server.on("/api/diagnostics", HTTP_GET, []() {
    sendNoCache(200, "application/json; charset=utf-8", DiagnosticsLog::recentJson());
  });
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
  server.on("/system/update-web", HTTP_POST, handleWebFilesystemUpdateComplete, handleWebFilesystemUpdateUpload);
  server.on("/system/update-latest", HTTP_POST, handleFirmwareUpdateLatest);
  server.on("/system/update-status", HTTP_GET, []() {
    String json = F("{\"active\":");
    json += otaActive ? F("true") : F("false");
    json += F(",\"stage\":\"");
    json += jsonEscape(otaStageName());
    json += F("\",\"component\":\"");
    json += jsonEscape(otaComponent);
    json += F("\",\"currentFirmware\":");
    json += String(otaCurrentFirmwareVersion);
    json += F(",\"latestFirmware\":");
    json += String(otaFirmwareVersion);
    json += F(",\"currentWeb\":");
    json += String(otaCurrentWebVersion);
    json += F(",\"latestWeb\":");
    json += String(otaWebVersion);
    json += F(",\"received\":");
    json += String(otaReceived);
    json += F(",\"total\":");
    json += String(otaTotal);
    json += F(",\"message\":\"");
    json += jsonEscape(otaMessage);
    json += F("\",\"error\":\"");
    json += jsonEscape(otaError);
    json += F("\"}");
    sendNoCache(200, "application/json; charset=utf-8", json);
  });
  server.on("/system/theme", HTTP_POST, []() {
    if (!server.hasArg("theme")) {
      sendNoCache(400, "application/json; charset=utf-8", "{\"message\":\"Theme is required.\"}");
      return;
    }
    const long value = server.arg("theme").toInt();
    DiagnosticsLog::line(String("WEB | THEME SAVE | ") + String(value));
    if (value < 0 || value >= THEME_COUNT || !saveTheme(static_cast<uint8_t>(value))) {
      sendNoCache(400, "application/json; charset=utf-8", "{\"message\":\"Invalid theme.\"}");
      return;
    }
    sendNoCache(204);
  });
  server.on("/system/reboot", HTTP_POST, []() { DiagnosticsLog::line("WEB | REBOOT REQUEST"); handleReboot(); });
  server.onNotFound([]() { sendNoCache(404, "text/plain", "Not found"); });
  server.begin();
  serverStarted = true;
  DiagnosticsLog::line("WEB | SERVER STARTED | port=80");
  Serial.println("Web server started on port 80.");
}

void service() {
  if (!serverStarted) return;
  server.handleClient();
  serviceOta();
}
}