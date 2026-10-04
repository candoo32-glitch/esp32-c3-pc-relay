#include "DiagnosticsLog.h"

namespace DiagnosticsLog {
namespace {
constexpr size_t HISTORY_LINES = 20;
String history[HISTORY_LINES];
size_t count = 0;
size_t next = 0;

void store(const String& message) {
  if (message.isEmpty()) return;
  history[next] = message;
  next = (next + 1) % HISTORY_LINES;
  if (count < HISTORY_LINES) ++count;
}

String escapeJson(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
      case '\\': escaped += F("\\\\"); break;
      case '"': escaped += '\\'; escaped += '"'; break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) escaped += ' ';
        else escaped += c;
        break;
    }
  }
  return escaped;
}
}

void remember(const String& message) {
  store(message);
}

void line(const String& message) {
  if (message.isEmpty()) return;
  Serial.println(message);
  store(message);
}

String recentJson() {
  String json;
  json.reserve(4200);
  json += F("{\"enabled\":true,\"lines\":[");
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) json += ',';
    const size_t index = (next + HISTORY_LINES - count + i) % HISTORY_LINES;
    json += '"';
    json += escapeJson(history[index]);
    json += '"';
  }
  json += F("]}");
  return json;
}

}
