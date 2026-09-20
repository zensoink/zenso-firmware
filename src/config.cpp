#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"

bool config_load(DeviceConfig &cfg) {
  if (!LittleFS.exists("/config.json")) {
    Serial.println("Config: /config.json not found");
    return false;
  }
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("Config: failed to open /config.json");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("Config: JSON parse error: %s\n", err.c_str());
    return false;
  }

  cfg.ssid = doc["ssid"] | "";
  cfg.password = doc["password"] | "";
  cfg.api_url = doc["api_url"] | "";
  if (cfg.api_url.endsWith("/")) cfg.api_url.remove(cfg.api_url.length() - 1);
  cfg.hardware_id = doc["hardware_id"] | "";
  cfg.device_secret = doc["device_secret"] | "";
  cfg.display_profile = doc["display_profile"] | "spectra6_7in3";

  Serial.println("Config: loaded successfully");
  return true;
}

bool config_save(const DeviceConfig &cfg) {
  JsonDocument doc;
  doc["ssid"] = cfg.ssid;
  doc["password"] = cfg.password;
  doc["api_url"] = cfg.api_url;
  doc["hardware_id"] = cfg.hardware_id;
  doc["device_secret"] = cfg.device_secret;
  doc["display_profile"] = cfg.display_profile.length() > 0 ? cfg.display_profile : "spectra6_7in3";
  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Config: failed to open /config.json for writing");
    return false;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Config: failed to serialize JSON");
    file.close();
    return false;
  }

  file.close();
  Serial.println("Config: saved successfully");
  return true;
}

bool config_is_provisioned(const DeviceConfig &cfg) {
  return cfg.ssid.length() > 0;
}
