#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "device_identity.h"

String device_get_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

DeviceIdentity identity_load() {
  DeviceIdentity identity;
  identity.device_id = device_get_id();
  identity.bootstrap_secret = identity.device_id;

  File file = LittleFS.open("/identity.json", "r");
  if (!file) {
    Serial.println("Identity: /identity.json not found, using defaults");
    return identity;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("Identity: JSON parse error: %s\n", err.c_str());
    return identity;
  }

  identity.device_id = doc["device_id"] | identity.device_id;
  identity.bootstrap_secret = doc["bootstrap_secret"] | identity.bootstrap_secret;
  identity.claim_session_id = doc["claim_session_id"] | "";
  identity.claim_url = doc["claim_url"] | "";
  identity.uid = doc["uid"] | "";
  identity.device_secret = doc["device_secret"] | "";

  Serial.println("Identity: loaded from /identity.json");
  return identity;
}

void identity_save(const DeviceIdentity &identity) {
  JsonDocument doc;
  doc["device_id"] = identity.device_id;
  doc["bootstrap_secret"] = identity.bootstrap_secret;
  doc["claim_session_id"] = identity.claim_session_id;
  doc["claim_url"] = identity.claim_url;
  doc["uid"] = identity.uid;
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
