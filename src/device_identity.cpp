#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "device_identity.h"

String normalize_hardware_id(const String &raw) {
  String result;
  result.reserve(raw.length());
  for (size_t i = 0; i < raw.length(); i++) {
    char c = raw.charAt(i);
    if (c != ':' && c != '-') {
      // NB: toupper() returns int; appending it directly would select
      // String::operator+=(int) and stringify the char code in decimal
      // (e.g. 'E' -> "69"). Cast back to char to append the character.
      result += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
  }
  return result;
}

String device_get_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[13];
  sprintf(buf, "%02X%02X%02X%02X%02X%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

DeviceIdentity identity_load() {
  DeviceIdentity identity;
  identity.hardware_id = device_get_id();
  identity.bootstrap_secret = identity.hardware_id;

  if (!LittleFS.exists("/identity.json")) {
    Serial.println("Identity: /identity.json not found, using defaults");
    return identity;
  }
  File file = LittleFS.open("/identity.json", "r");
  if (!file) {
    Serial.println("Identity: failed to open /identity.json");
    return identity;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("Identity: JSON parse error: %s\n", err.c_str());
    return identity;
  }

  identity.bootstrap_secret = doc["bootstrap_secret"] | identity.bootstrap_secret;
  identity.claim_session_id = doc["claim_session_id"] | "";
  identity.claim_url = doc["claim_url"] | "";
  identity.hardware_id = doc["hardware_id"] | "";
  identity.device_secret = doc["device_secret"] | "";

  Serial.println("Identity: loaded from /identity.json");
  return identity;
}

void identity_save(const DeviceIdentity &identity) {
  JsonDocument doc;
  doc["bootstrap_secret"] = identity.bootstrap_secret;
  doc["claim_session_id"] = identity.claim_session_id;
  doc["claim_url"] = identity.claim_url;
  doc["hardware_id"] = identity.hardware_id;
  doc["device_secret"] = identity.device_secret;

  File file = LittleFS.open("/identity.json", "w");
  if (!file) {
    Serial.println("Identity: failed to open /identity.json for writing");
    return;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Identity: failed to serialize JSON");
  } else {
    Serial.println("Identity: saved to /identity.json");
  }

  file.close();
}
