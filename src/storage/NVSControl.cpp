#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "NVSControl.h"
#include "../interface/Console.h"

namespace NVSControl {

namespace {
constexpr char NVS_PARTITION[] = "nvs";

void color(const char* code) {
  if (Console::ansiSupported) Console::color(code);
}

void reset() {
  if (Console::ansiSupported) Console::resetStyle();
}

void printHeader(const char* title) {
  Serial.println();
  color("1;36m");
  Serial.println("+---------------------------------------------+");
  Serial.printf("| %-43s |\r\n", title);
  Serial.println("+---------------------------------------------+");
  reset();
}

void printType(nvs_type_t type) {
  switch (type) {
    case NVS_TYPE_U8:  Serial.print("U8"); break;
    case NVS_TYPE_I8:  Serial.print("I8"); break;
    case NVS_TYPE_U16: Serial.print("U16"); break;
    case NVS_TYPE_I16: Serial.print("I16"); break;
    case NVS_TYPE_U32: Serial.print("U32"); break;
    case NVS_TYPE_I32: Serial.print("I32"); break;
    case NVS_TYPE_U64: Serial.print("U64"); break;
    case NVS_TYPE_I64: Serial.print("I64"); break;
    case NVS_TYPE_STR: Serial.print("STRING"); break;
    case NVS_TYPE_BLOB: Serial.print("BLOB"); break;
    default: Serial.print("UNKNOWN"); break;
  }
}

bool isSensitiveKey(const char* key) {
  return strcasecmp(key, "password") == 0 ||
         strcasecmp(key, "passphrase") == 0 ||
         strcasecmp(key, "token") == 0;
}

void printEntry(const nvs_entry_info_t& entry, size_t number) {
  nvs_handle_t handle = 0;
  const esp_err_t openResult =
      nvs_open_from_partition(NVS_PARTITION, entry.namespace_name,
                              NVS_READONLY, &handle);

  String value = "<read error>";
  if (openResult == ESP_OK) {
    if (isSensitiveKey(entry.key)) {
      value = "<hidden>";
    } else {
      switch (entry.type) {
        case NVS_TYPE_U8: { uint8_t v; if (nvs_get_u8(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_I8: { int8_t v; if (nvs_get_i8(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_I16: { int16_t v; if (nvs_get_i16(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_I32: { int32_t v; if (nvs_get_i32(handle, entry.key, &v) == ESP_OK) value = String(v); break; }
        case NVS_TYPE_U64: { uint64_t v; if (nvs_get_u64(handle, entry.key, &v) == ESP_OK) value = String((unsigned long long)v); break; }
        case NVS_TYPE_I64: { int64_t v; if (nvs_get_i64(handle, entry.key, &v) == ESP_OK) value = String((long long)v); break; }
        case NVS_TYPE_STR: {
          size_t len = 0;
          if (nvs_get_str(handle, entry.key, nullptr, &len) == ESP_OK && len > 0) {
            char* buffer = new char[len];
            if (nvs_get_str(handle, entry.key, buffer, &len) == ESP_OK) value = buffer;
            delete[] buffer;
          } else value = "<empty>";
          break;
        }
        case NVS_TYPE_BLOB: {
          size_t len = 0;
          if (nvs_get_blob(handle, entry.key, nullptr, &len) == ESP_OK)
            value = String("<") + String((unsigned)len) + " bytes>";
          break;
        }
        default: value = "<unsupported>"; break;
      }
    }
    nvs_close(handle);
  } else {
    value = String("ERROR: ") + esp_err_to_name(openResult);
  }

  constexpr size_t VALUE_WIDTH = 20;
  if (value.length() > VALUE_WIDTH) value = value.substring(0, VALUE_WIDTH);

  // Keep the entire table at exactly 78 columns for standard 80-column
  // terminals. Every field, including TYPE, has a fixed width.
  color("1;36m");
  Serial.printf("| %-3u | ", static_cast<unsigned>(number));
  color("1;37m");
  Serial.printf("%-14.14s", entry.namespace_name);
  color("1;36m");
  Serial.print(" | ");
  color("1;35m");
  Serial.printf("%-19.19s", entry.key);
  color("1;36m");
  Serial.print(" | ");
  color("1;33m");
  printType(entry.type);
  const size_t typeLength = strlen(
      entry.type == NVS_TYPE_STR ? "STRING" :
      entry.type == NVS_TYPE_BLOB ? "BLOB" :
      entry.type == NVS_TYPE_U8 ? "U8" :
      entry.type == NVS_TYPE_I8 ? "I8" :
      entry.type == NVS_TYPE_U16 ? "U16" :
      entry.type == NVS_TYPE_I16 ? "I16" :
      entry.type == NVS_TYPE_U32 ? "U32" :
      entry.type == NVS_TYPE_I32 ? "I32" :
      entry.type == NVS_TYPE_U64 ? "U64" :
      entry.type == NVS_TYPE_I64 ? "I64" : "UNKNOWN");
  for (size_t i = typeLength; i < 7; ++i) Serial.print(' ');
  color("1;36m");
  Serial.print(" | ");
  if (isSensitiveKey(entry.key)) color("1;31m");
  else color("1;37m");
  Serial.printf("%-20s", value.c_str());
  color("1;36m");
  Serial.println(" |");
  reset();
}

void viewContents() {
  printHeader("NVS CONTENTS");
  color("1;37m");
  Serial.println("Partition: nvs");
  Serial.println();
  color("1;36m");
  Serial.println("  Serial.println("| #   | NAMESPACE       | KEY                  | TYPE    | VALUE                 |");
  Serial.println("  nvs_iterator_t iterator = nullptr;
  size_t count = 0;

  esp_err_t findResult =
      nvs_entry_find(NVS_PARTITION, nullptr, NVS_TYPE_ANY, &iterator);
  while (findResult == ESP_OK && iterator != nullptr) {
    nvs_entry_info_t info;
    nvs_entry_info(iterator, &info);
    printEntry(info, ++count);
    findResult = nvs_entry_next(&iterator);
  }

  if (iterator != nullptr) {
    nvs_release_iterator(iterator);
  }

  color("1;36m");
  Serial.println("  if (count == 0) {
    color("1;33m");
    Serial.println("  NVS is empty.");
    reset();
  } else {
    Serial.println();
    color("1;32m");
    Serial.printf("  %u entries.\r\n", static_cast<unsigned>(count));
    reset();
  }

  Serial.println();
  color("1;33m");
  Serial.println("Password/token values are hidden.");
  reset();
  Serial.println("B. Back");
  Serial.println();

  Console::readMenuChoice("Press B to return: ", "B");
}

void formatNVS() {
  printHeader("FORMAT NVS");
  color("1;31m");
  Serial.println("WARNING: This erases the entire NVS partition.");
  Serial.println("WiFi credentials, TX power, diagnostics, network settings,");
  Serial.println("and all other NVS-stored configuration will be deleted.");
  reset();
  Serial.println();
  color("1;33m");
  Serial.println("The ESP32 will reboot after a successful format.");
  reset();
  Serial.println();

  String choice = Console::readMenuChoice("Type F to confirm: ", "F");
  choice.trim();
  choice.toUpperCase();
  if (choice != "F") {
    Serial.println("Format cancelled.");
    return;
  }

  const esp_err_t result = nvs_flash_erase_partition(NVS_PARTITION);
  if (result != ESP_OK) {
    color("1;31m");
    Serial.print("NVS format FAILED: ");
    Serial.println(esp_err_to_name(result));
    reset();
    return;
  }

  const esp_err_t initResult = nvs_flash_init_partition(NVS_PARTITION);
  if (initResult != ESP_OK) {
    color("1;31m");
    Serial.print("NVS re-initialization FAILED: ");
    Serial.println(esp_err_to_name(initResult));
    reset();
    return;
  }

  color("1;32m");
  Serial.println();
  Serial.println("NVS FORMAT SUCCESSFUL");
  reset();
  Serial.println("All NVS data has been erased.");
  Serial.println("Rebooting...");
  delay(1000);
  ESP.restart();
}

} // namespace

void menu() {
  while (true) {
    printHeader("NVS MANAGEMENT");
    color("1;36m");
    Serial.println("1. View NVS contents");
    reset();
    color("1;31m");
    Serial.println("2. Format NVS");
    reset();
    Serial.println("B. Back");
    Serial.println();

    String choice = Console::readMenuChoice("Select: ", "12B");
    choice.trim();
    choice.toUpperCase();

    if (choice == "1") {
      viewContents();
    } else if (choice == "2") {
      formatNVS();
    } else if (choice == "B") {
      return;
    }
  }
}

} // namespace NVSControl
