#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <memory>
#include "config.h"
#include "device_identity.h"
#include "http_client.h"
#include "version.h"

// ── Helpers ──

static WiFiClient* make_client(const String &url) {
  if (url.startsWith("https://")) {
    auto *c = new WiFiClientSecure();
    c->setInsecure();
    return c;
  }
  return new WiFiClient();
}

static bool validate_config(const DeviceConfig &cfg) {
  String missing;
  if (cfg.api_url.length() == 0) missing += "api_url";
  if (cfg.device_secret.length() == 0) {
    if (missing.length() > 0) missing += ", ";
    missing += "device_secret";
  }
  if (missing.length() > 0) {
    Serial.printf("[HttpClient] Missing required config: %s\n", missing.c_str());
    return false;
  }
  return true;
}

// ── Login ──

bool device_login(const DeviceConfig &cfg, String &out_token) {
  if (!validate_config(cfg)) return false;

  String login_url = cfg.api_url + "/auth/device/login";
  std::unique_ptr<WiFiClient> client(make_client(login_url));
  HTTPClient http;
  http.begin(*client, login_url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument login_doc;
  login_doc["hardware_id"] = device_get_id();
  login_doc["secret"] = cfg.device_secret;
  String login_body;
  serializeJson(login_doc, login_body);

  int login_code = http.POST(login_body);
  if (login_code != 200) {
    Serial.printf("[HttpClient] Login failed: HTTP %d\n", login_code);
    http.end();
    return false;
  }

  String login_resp = http.getString();
  http.end();

  JsonDocument token_doc;
  DeserializationError err = deserializeJson(token_doc, login_resp);
  if (err) {
    Serial.printf("[HttpClient] Login JSON parse error: %s\n", err.c_str());
    return false;
  }

  out_token = token_doc["accessToken"].as<String>();
  if (out_token.length() == 0) {
    Serial.printf("[HttpClient] Login response missing accessToken\n");
    return false;
  }
  Serial.printf("[HttpClient] Login OK, token=%d chars\n", out_token.length());
  return true;
}

// ── Check-in ──

FetchResult http_check_in(const DeviceConfig &cfg, const String &token, DeviceStatus &out_status) {
  if (!validate_config(cfg)) return FetchResult::ERROR;

  // Defaults
  out_status = { -1, 300, false, false, "", 800, 480 };

  String url = cfg.api_url + "/devices/check-in";
  std::unique_ptr<WiFiClient> client(make_client(url));
  HTTPClient http;
  http.begin(*client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);

  JsonDocument body_doc;
  body_doc["firmwareVersion"] = FIRMWARE_VERSION;
  String body;
  serializeJson(body_doc, body);

  int code = http.POST(body);
  if (code == 401) {
    Serial.println("[HttpClient] Check-in 401 — token expired");
    http.end();
    return FetchResult::ERROR;
  }
  if (code < 200 || code > 299) {
    Serial.printf("[HttpClient] Check-in failed: HTTP %d\n", code);
    http.end();
    return FetchResult::ERROR;
  }

  String resp = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("[HttpClient] Check-in JSON parse error: %s\n", err.c_str());
    return FetchResult::ERROR;
  }

  out_status.screen_id = doc["screenId"].is<int>() ? doc["screenId"].as<int>() : -1;
  out_status.refresh_rate = doc["refreshRate"].as<int>();
  out_status.has_image = doc["hasImage"].as<bool>();
  out_status.content_changed = doc["contentChanged"].as<bool>();
  out_status.display_profile = doc["displayProfile"].as<String>();
  out_status.width = doc["width"].is<int>() ? doc["width"].as<int>() : 800;
  out_status.height = doc["height"].is<int>() ? doc["height"].as<int>() : 480;

  if (out_status.refresh_rate <= 0) out_status.refresh_rate = 300;

  Serial.printf("[HttpClient] Check-in OK: screenId=%d refreshRate=%d hasImage=%d contentChanged=%d profile=%s (%dx%d)\n",
                out_status.screen_id, out_status.refresh_rate,
                out_status.has_image, out_status.content_changed,
                out_status.display_profile.c_str(), out_status.width, out_status.height);
  return FetchResult::OK;
}

// ── Fetch display (with token) ──

FetchResult http_fetch_display_with_token(const DeviceConfig &cfg, const String &token) {
  if (!validate_config(cfg)) return FetchResult::ERROR;

  HTTPClient http;

  // Load saved ETag
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

  String display_url = cfg.api_url + "/devices/display";
  Serial.printf("[HttpClient] Fetching display from: %s (free heap: %u bytes)\n", display_url.c_str(), (unsigned)ESP.getFreeHeap());

  std::unique_ptr<WiFiClient> client(make_client(display_url));
  client->setTimeout(30);
  http.setTimeout(30000);
  http.begin(*client, display_url);
  http.addHeader("Authorization", "Bearer " + token);
  if (etag.length() > 0) {
    http.addHeader("If-None-Match", etag);
  }

  const char *headerKeys[] = {"ETag", "Content-Length"};
  http.collectHeaders(headerKeys, 2);

  Serial.println("[HttpClient] Sending GET /devices/display...");
  int display_code = http.GET();
  Serial.printf("[HttpClient] GET %s -> HTTP %d (%s)\n", display_url.c_str(), display_code, http.errorToString(display_code).c_str());

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
    Serial.printf("[HttpClient] Display fetch failed: HTTP %d (%s)\n", display_code, http.errorToString(display_code).c_str());
    http.end();
    return FetchResult::ERROR;
  }

  int content_length = http.getSize();
  Serial.printf("[HttpClient] Connected to stream, Content-Length: %d bytes\n", content_length);

  // Stream body to /display.raw
  WiFiClient *stream = http.getStreamPtr();
  File file = LittleFS.open("/display.raw", "w");
  if (!file) {
    Serial.printf("[HttpClient] Failed to open /display.raw for writing\n");
    http.end();
    return FetchResult::ERROR;
  }

  uint8_t buf[1024];
  size_t total = 0;
  unsigned long last_progress_ms = millis();
  unsigned long last_read_ms = millis();

  while (content_length > 0 ? (total < (size_t)content_length) : (http.connected() || stream->available())) {
    bool bytes_read = false;
    while (stream->available()) {
      size_t to_read = sizeof(buf);
      if (content_length > 0 && ((size_t)content_length - total) < to_read) {
        to_read = (size_t)content_length - total;
      }
      int len = stream->read(buf, to_read);
      if (len > 0) {
        file.write(buf, len);
        total += len;
        bytes_read = true;
        last_read_ms = millis();
      }
      if (content_length > 0 && total >= (size_t)content_length) {
        break;
      }
    }

    if (content_length > 0 && total >= (size_t)content_length) {
      break;
    }

    if (millis() - last_progress_ms > 1000 && total > 0) {
      if (content_length > 0) {
        Serial.printf("[HttpClient] Streamed %u / %d bytes (%u%%)...\n",
                      (unsigned)total, content_length, (unsigned)(total * 100 / content_length));
      } else {
        Serial.printf("[HttpClient] Streamed %u bytes...\n", (unsigned)total);
      }
      last_progress_ms = millis();
    }

    // Guard against socket hang (10s with no data)
    if (!bytes_read && (millis() - last_read_ms > 10000)) {
      Serial.println("[HttpClient] Stream read timeout (10s without data)");
      break;
    }

    delay(1);
  }
  file.close();

  if (total == 0) {
    Serial.printf("[HttpClient] 0 bytes written to /display.raw\n");
    http.end();
    return FetchResult::ERROR;
  }
  Serial.printf("[HttpClient] Download complete: %u bytes written to /display.raw\n", (unsigned)total);

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

// ── Legacy wrapper (login + fetch, used during initial boot) ──

FetchResult http_fetch_display(const DeviceConfig &cfg) {
  String token;
  if (!device_login(cfg, token)) return FetchResult::ERROR;
  return http_fetch_display_with_token(cfg, token);
}
