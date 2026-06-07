#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "http_client.h"

FetchResult http_fetch_display(const DeviceConfig &cfg) {
  // Validate config
  String missing;
  if (cfg.api_url.length() == 0) missing += "api_url";
  if (cfg.device_uid.length() == 0) {
    if (missing.length() > 0) missing += ", ";
    missing += "device_uid";
  }
  if (cfg.device_secret.length() == 0) {
    if (missing.length() > 0) missing += ", ";
    missing += "device_secret";
  }
  if (missing.length() > 0) {
    Serial.printf("[HttpClient] Missing required config: %s\n", missing.c_str());
    return FetchResult::ERROR;
  }

  WiFiClient client;
  HTTPClient http;

  // -- Device login --
  String login_url = cfg.api_url + "/auth/device/login";
  http.begin(client, login_url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument login_doc;
  login_doc["uid"] = cfg.device_uid;
  login_doc["secret"] = cfg.device_secret;
  String login_body;
  serializeJson(login_doc, login_body);

  int login_code = http.POST(login_body);
  if (login_code != 200) {
    Serial.printf("[HttpClient] Login failed: HTTP %d\n", login_code);
    http.end();
    return FetchResult::ERROR;
  }

  String login_resp = http.getString();
  http.end();

  JsonDocument token_doc;
  DeserializationError err = deserializeJson(token_doc, login_resp);
  if (err) {
    Serial.printf("[HttpClient] Login JSON parse error: %s\n", err.c_str());
    return FetchResult::ERROR;
  }

  String token = token_doc["accessToken"].as<String>();
  if (token.length() == 0) {
    Serial.printf("[HttpClient] Login response missing accessToken\n");
    return FetchResult::ERROR;
  }
  Serial.printf("[HttpClient] Login OK, token=%d chars\n", token.length());

  // -- Load saved ETag --
  String etag;
  File etag_file = LittleFS.open("/display.etag", "r");
  if (etag_file) {
    etag = etag_file.readString();
    etag_file.close();
    etag.trim();
    if (etag.length() > 0) {
      Serial.printf("[HttpClient] Loaded etag: %s\n", etag.c_str());
    }
  }

  // -- Fetch display --
  String display_url = cfg.api_url + "/devices/" + cfg.device_uid + "/display";
  http.begin(client, display_url);
  http.addHeader("Authorization", "Bearer " + token);
  if (etag.length() > 0) {
    http.addHeader("If-None-Match", etag);
  }

  const char *headerKeys[] = {"ETag"};
  http.collectHeaders(headerKeys, 1);

  int display_code = http.GET();
  Serial.printf("[HttpClient] GET %s -> HTTP %d\n", display_url.c_str(), display_code);

  if (display_code == 304) {
    Serial.printf("[HttpClient] 304 Not Modified - reusing cache\n");
    http.end();
    return FetchResult::NOT_MODIFIED;
  }

  if (display_code == 404) {
    Serial.println("[HttpClient] 404 — no screen assigned to this device");
    http.end();
    return FetchResult::NO_CONTENT;
  }

  if (display_code != 200) {
    Serial.printf("[HttpClient] Display fetch failed: HTTP %d\n", display_code);
    http.end();
    return FetchResult::ERROR;
  }

  // Stream body to /display.raw
  WiFiClient *stream = http.getStreamPtr();
  File file = LittleFS.open("/display.raw", "w");
  if (!file) {
    Serial.printf("[HttpClient] Failed to open /display.raw for writing\n");
    http.end();
    return FetchResult::ERROR;
  }

  uint8_t buf[512];
  size_t total = 0;
  while (http.connected() || stream->available()) {
    while (stream->available()) {
      int len = stream->read(buf, sizeof(buf));
      if (len > 0) {
        file.write(buf, len);
        total += len;
      }
    }
  }
  file.close();

  if (total == 0) {
    Serial.printf("[HttpClient] 0 bytes written to /display.raw\n");
    http.end();
    return FetchResult::ERROR;
  }
  Serial.printf("[HttpClient] Written %u bytes to /display.raw\n", (unsigned)total);

  // Save ETag if present
  String new_etag = http.header("ETag");
  if (new_etag.length() > 0) {
    File ef = LittleFS.open("/display.etag", "w");
    if (ef) {
      ef.print(new_etag);
      ef.close();
      Serial.printf("[HttpClient] Saved etag: %s\n", new_etag.c_str());
    }
  }

  http.end();
  return FetchResult::OK;
}
