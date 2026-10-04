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

namespace WebControl {
namespace {
// OTA simultaneous firmware + Web UI combined OTA test build marker.
WebServer server(80);
bool serverStarted = false;

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
  Serial.print("OTA failed: ");
  Serial.println(message);
}

void completeOta() {
  otaActive = false;
  setOtaStatus(OtaStage::COMPLETE, "", "Firmware and Web UI updates are complete.",
               otaReceived, otaTotal);
  Serial.println("OTA operation complete.");
}

bool beginOtaDownload(const String& url, const char* component, OtaStage downloadStage,
                      OtaStage writeStage) {
  setOtaStatus(downloadStage, component,
               String("Downloading ") + component + ".");

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
  Serial.println("OTA updates installed successfully; rebooting.");
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
        setOtaStatus(OtaStage::COMPLETE, "",
                     "Firmware and Web UI are already up to date.");
        return;
      }

      if (otaFirmwarePending) {
        Serial.print("OTA firmware update found: build ");
        Serial.println(otaFirmwareVersion);
      }
      if (otaWebPending) {
        Serial.print("OTA Web UI update found: build ");
        Serial.println(otaWebVersion);
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
    server.send(409, "application/json; charset=utf-8",
                "{\"message\":\"An OTA update is already in progress.\"}");
    return;
  }

  if (!WiFiControl::isEnabled() || WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json; charset=utf-8",
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
  setOtaStatus(OtaStage::CHECKING, "",
               "Checking GitHub for firmware and Web UI updates.");

  server.send(202, "application/json; charset=utf-8",
              "{\"message\":\"OTA update started.\"}");
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

void handleFirmwareUpdateComplete(){if(firmwareUpdateFailed||firmwareUpdateBytes==0){if(Update.isRunning())Update.abort();server.send(400,"application/json; charset=utf-8","{\"message\":\"The firmware image could not be uploaded or verified. The existing firmware was not replaced.\"}");return;}server.send(200,"application/json; charset=utf-8","{\"message\":\"Firmware upgraded successfully. The ESP32-C3 is rebooting now.\"}");server.client().flush();delay(1000);ESP.restart();}

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

void handleConfigRestoreComplete(){const esp_partition_t* p=nvsPartition();const bool v=!restoreFailed&&p&&restoreBuffer&&restoreBytes==restoreCapacity;if(!v){if(restoreBuffer)free(restoreBuffer);restoreBuffer=nullptr;restoreCapacity=0;restoreBytes=0;server.send(400,"application/json; charset=utf-8","{\"message\":\"The uploaded NVS backup was incomplete or invalid. No configuration was changed.\"}");return;}esp_err_t r=esp_partition_erase_range(p,0,p->size);if(r==ESP_OK)r=esp_partition_write(p,0,restoreBuffer,p->size);free(restoreBuffer);restoreBuffer=nullptr;restoreCapacity=0;restoreBytes=0;if(r!=ESP_OK){server.send(500,"application/json; charset=utf-8","{\"message\":\"NVS configuration restore failed.\"}");return;}server.send(200,"application/json; charset=utf-8","{\"message\":\"Configuration restored successfully. The ESP32-C3 is rebooting now.\"}");server.client().flush();delay(1000);ESP.restart();}

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
  json += F(",\"theme\":");
  json += String(savedTheme());
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

void handleToggle(){const bool enable=!WiFiControl::isEnabled();if(!enable){server.send(204);delay(100);WiFiControl::setEnabled(false);return;}WiFiControl::setEnabled(true);server.send(204);}

void handleReconnect() {
  WiFiControl::connect();
  server.send(204);
}

void handleWifiSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(204);
    return;
  }
  const String ssid = server.arg("ssid");
  String password = server.arg("password");
  if (password == "********") {
    WiFiControl::configureSavedCredentials(ssid);
  } else {
    WiFiControl::configureCredentials(ssid, password);
  }
  server.send(204);
}

void handleWifiScan(){if(!WiFiControl::isEnabled()){server.send(409,"application/json; charset=utf-8","{\"networks\":[]}");return;}WiFiControl::service();WiFi.scanDelete();const int count=WiFi.scanNetworks();String j=F("{\"networks\":[");for(int n=0;n<count;++n){if(n)j+=',';j+=F("{\"ssid\":\"");j+=jsonEscape(WiFi.SSID(n));j+=F("\",\"rssi\":");j+=String(WiFi.RSSI(n));j+=F(",\"channel\":");j+=String(WiFi.channel(n));j+=F(",\"security\":\"");j+=jsonEscape(WiFiControl::authModeName(WiFi.encryptionType(n)));j+=F("\",\"bssid\":\"");j+=jsonEscape(WiFi.BSSIDstr(n));j+=F("\"}");}j+=F("]}");WiFi.scanDelete();server.send(200,"application/json; charset=utf-8",j);}

void handleTxPower() {
  if (server.hasArg("dbm")) {
    const float value = server.arg("dbm").toFloat();
    WiFiControl::setTxPowerDbm(value);
  }
  server.send(204);
}

void handleDiagnosticsToggle() {
  const bool desired = !WiFiDiagnostics::enabled();
  if (desired != WiFiDiagnostics::enabled()) WiFiDiagnostics::toggle();
  server.send(204);
}

void handleNetworkSave() {
  if (server.hasArg("hostname")) {
    if (!NetConfig::setHostname(server.arg("hostname"))) {
      server.send(204);
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
  server.send(204);
}

void handleRelayAction() {
  if (!server.hasArg("id") || !server.hasArg("action")) {
    server.send(204);
    return;
  }
  const int id = server.arg("id").toInt();
  if (id < 0 || id > 1) {
    server.send(204);
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
  server.send(204);
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
      server.send(204);
      return;
    }

    String normal = server.hasArg(prefix + "normal") ? server.arg(prefix + "normal") : "";
    normal.trim();
    if (normal.isEmpty()) normal = "open";
    if (normal == "open") Relay::setNormalState(relay, Relay::NormalState::OPEN);
    else if (normal == "closed") Relay::setNormalState(relay, Relay::NormalState::CLOSED);
    else {
      server.send(204);
      return;
    }

    String mode = server.hasArg(prefix + "mode") ? server.arg(prefix + "mode") : "";
    mode.trim();
    if (mode.isEmpty()) mode = "latched";
    if (mode == "latched") Relay::setActivationMode(relay, Relay::ActivationMode::LATCHED);
    else if (mode == "pulse") Relay::setActivationMode(relay, Relay::ActivationMode::PULSE);
    else {
      server.send(204);
      return;
    }

    String pulseText = server.hasArg(prefix + "pulse") ? server.arg(prefix + "pulse") : "";
    pulseText.trim();
    uint32_t pulse = defaults[i].pulse;
    if (!pulseText.isEmpty()) {
      const long parsed = pulseText.toInt();
      if (parsed >= 10 && parsed <= 60000) pulse = static_cast<uint32_t>(parsed);
      else {
        server.send(204);
        return;
      }
    }
    if (!Relay::setPulseMs(relay, pulse)) {
      server.send(204);
      return;
    }
  }

  server.send(204);
}

void handleNvsFormat(){server.send(204);delay(300);const esp_err_t r=nvs_flash_erase_partition("nvs");if(r==ESP_OK)nvs_flash_init_partition("nvs");delay(300);ESP.restart();}

void handleReboot(){server.send(204);delay(300);ESP.restart();}

} // namespace

void begin() {
  if (serverStarted) return;

  if (!SPIFFS.begin(false)) {
    Serial.println("Web UI filesystem mount failed.");
  } else {
    loadWebInterfaceBuild();
    Serial.println("Web UI filesystem mounted.");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/style.css", HTTP_GET, handleStyleCss);
  server.on("/app.js", HTTP_GET, handleAppJs);
  server.on("/api/state", HTTP_GET, handlePageState);
  server.on("/api/diagnostics", HTTP_GET, []() {
    server.send(200, "application/json; charset=utf-8", WiFiDiagnostics::recentLogJson());
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
    server.send(200, "application/json; charset=utf-8", json);
  });
  server.on("/system/theme", HTTP_POST, []() {
    if (!server.hasArg("theme")) {
      server.send(400, "application/json; charset=utf-8", "{\"message\":\"Theme is required.\"}");
      return;
    }
    const long value = server.arg("theme").toInt();
    if (value < 0 || value >= THEME_COUNT || !saveTheme(static_cast<uint8_t>(value))) {
      server.send(400, "application/json; charset=utf-8", "{\"message\":\"Invalid theme.\"}");
      return;
    }
    server.send(204);
  });
  server.on("/system/reboot", HTTP_POST, handleReboot);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  serverStarted = true;
  Serial.println("Web server started on port 80.");
}

void service() {
  if (!serverStarted) return;
  server.handleClient();
  serviceOta();
}
}