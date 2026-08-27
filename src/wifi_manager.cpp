#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "config.h"
#include "version.h"
#include "provisioning_screen.h"
#include "device_identity.h"
#include "portal_html.h"
#include "pins.h"
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

extern void clearToWhite();


static String provisioning_generate_password() {
  static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  String pwd;
  for (uint8_t i = 0; i < 10; i++) {
    pwd += chars[esp_random() % (sizeof(chars) - 1)];
  }
  return pwd;
}

bool wifi_connect(const DeviceConfig &cfg, uint32_t timeout_ms) {
  Serial.printf("WiFi: connecting to %s...\n", cfg.ssid.c_str());

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());

  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi: connected, IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.printf("WiFi: connection to %s timed out after %lums\n",
                cfg.ssid.c_str(), (unsigned long)timeout_ms);
  return false;
}

void wifi_start_provisioning(DeviceConfig &cfg) {
  Serial.println("Provisioning: starting captive portal...");

  String ap_password = provisioning_generate_password();
  String hardware_id = device_get_id();

  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  bool ap_ok = WiFi.softAP("Zenso-Setup", ap_password.c_str());
  delay(500);

  Serial.printf("Provisioning: WiFi mode=%d\n", WiFi.getMode());
  Serial.printf("Provisioning: softAP result=%d\n", ap_ok ? 1 : 0);
  Serial.printf("Provisioning: SSID=Zenso-Setup\n");
  Serial.printf("Provisioning: Password=%s\n", ap_password.c_str());
  Serial.println("Provisioning: Device ID=" + hardware_id);
  Serial.printf("Provisioning: AP IP=%s\n", WiFi.softAPIP().toString().c_str());

  if (!ap_ok) {
    Serial.println("Provisioning: FATAL — SoftAP failed to start, halting");
    while (true) {
      delay(1000);
    }
  }

  DNSServer dns;
  dns.start(53, "*", WiFi.softAPIP());

  WebServer server(80);

  // iOS captive portal detection paths — redirect to setup form
  const char* captive_paths[] = {
    "/generate_204", "/hotspot-detect.html", "/library/test/success.html",
    "/success.txt", "/ncsi.txt", "/connecttest.txt",
    "/redirect", "/canonical.html"
  };
  for (const char* path : captive_paths) {
    server.on(path, [&server]() {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/html", REDIRECT_HTML);
    });
  }

  server.on("/", [&server]() {
    server.send(200, "text/html", PROVISIONING_HTML);
  });

  String claim_url;
  server.on("/save", HTTP_POST, [&cfg, &server, &claim_url]() {
    cfg.ssid = server.arg("ssid");
    cfg.password = server.arg("password");
    cfg.api_url = server.arg("api_url");

    config_save(cfg);
    Serial.println("Provisioning: config saved");

    if (!wifi_connect(cfg, 15000)) {
      Serial.println("Provisioning: WiFi connection failed");
      server.send(200, "text/html", portal_html_wifi_failed(cfg.ssid));
      return;
    }

    Serial.printf("Provisioning: WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

    delay(500);

    WiFiClient client;
    HTTPClient http;
    String bootstrap_url = cfg.api_url + "/device/bootstrap";
    http.begin(client, bootstrap_url);
    http.addHeader("Content-Type", "application/json");

    String hardware_id = normalize_hardware_id(device_get_id());
    JsonDocument body;
    body["hardware_id"] = hardware_id;
    body["firmware_version"] = FIRMWARE_VERSION;
    body["hardware_info"] = JsonObject();
    JsonObject display_info = body["display_info"].to<JsonObject>();
    display_info["width"] = 800;
    display_info["height"] = 480;
    display_info["colors"] = 7;

    String request_body;
    serializeJson(body, request_body);

    int http_code = http.POST(request_body);
    String response_body = http.getString();
    http.end();

    if (http_code != 200 && http_code != 201) {
      Serial.printf("Provisioning: bootstrap HTTP %d\n", http_code);
      server.send(200, "text/html", portal_html_server_error(http_code));
      return;
    }

    JsonDocument resp;
    DeserializationError json_err = deserializeJson(resp, response_body);
    if (json_err) {
      Serial.printf("Provisioning: bootstrap JSON parse error: %s\n", json_err.c_str());
      server.send(200, "text/html", portal_html_parse_error());
      return;
    }

    if (resp["claim_url"].isNull() || resp["claim_session_id"].isNull()) {
      Serial.println("Provisioning: bootstrap returned no claim session — device may already be active");
      server.send(200, "text/html", portal_html_parse_error());
      delay(5000);
      ESP.restart();
      return;
    }

    // try snake_case first, fall back to camelCase
    claim_url = resp["claim_url"].as<String>();
    if (claim_url.length() == 0) claim_url = resp["claimUrl"].as<String>();

    String claim_session_id = resp["claim_session_id"].as<String>();
    if (claim_session_id.length() == 0) claim_session_id = resp["claimSessionId"].as<String>();

    String claim_expires_at = resp["claim_expires_at"].as<String>();
    if (claim_expires_at.length() == 0) claim_expires_at = resp["claimExpiresAt"].as<String>();

    Serial.println("Provisioning: claim_url=" + claim_url);
    Serial.println("Provisioning: claim_session_id=" + claim_session_id);
    Serial.println("Provisioning: claim_expires_at=" + claim_expires_at);

    if (claim_url.length() == 0 || claim_session_id.length() == 0) {
      Serial.println("Provisioning: bootstrap response missing claim_url or claim_session_id");
      server.send(200, "text/html", portal_html_server_error(0));
      return;
    }

    if (claim_url.indexOf("localhost") >= 0) {
      Serial.println("Provisioning: WARNING — claim_url contains 'localhost'. Set APP_BASE_URL to a real IP in the API .env file.");
    }

    {
      DeviceIdentity identity = identity_load();
      identity.claim_session_id = claim_session_id;
      identity.claim_url = claim_url;
      identity_save(identity);
      Serial.println("Provisioning: identity saved with claim data");
    }

    provisioning_screen_draw_waiting(claim_url, WiFi.localIP().toString());
    Serial.println("Provisioning: claim URL = " + claim_url);

    server.send(200, "text/html",
      portal_html_success(cfg.ssid, WiFi.localIP().toString(), claim_url, claim_expires_at));
    Serial.println("Provisioning: bootstrap success, portal continues");
  });

  server.onNotFound([&server]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/html", REDIRECT_HTML);
  });

  server.begin();
  Serial.println("Provisioning: HTTP server started on port 80");

  provisioning_screen_init();

  provisioning_screen_draw(
    "Zenso-Setup",
    ap_password,
    "http://192.168.4.1",
    FIRMWARE_VERSION
  );

  // Claim polling loop — 30s interval
  uint32_t last_poll = 0;
  const uint32_t POLL_INTERVAL_MS = 30000;
  bool claim_done = false;
  int active_without_hardware_id_count = 0;
  int active_without_secret_count = 0;

  while (!claim_done) {
    dns.processNextRequest();
    server.handleClient();

    // KEY1 press → whiten the welcome/waiting screen (non-persistent; replug redraws welcome)
    if (digitalRead(KEY1_PIN) == LOW) {
      clearToWhite();
      while (digitalRead(KEY1_PIN) == LOW) delay(50); // wait for release
    }

    uint32_t now = millis();
    if (now - last_poll >= POLL_INTERVAL_MS) {
      last_poll = now;

      // Load claim_session_id from identity
      DeviceIdentity identity = identity_load();
      if (identity.claim_session_id.length() == 0) {
        Serial.println("Polling: no claim_session_id, skipping");
        // no delay — last_poll is already set, next poll in POLL_INTERVAL_MS
        continue;
      }

      // GET /device/claim-status/:id
      WiFiClient poll_client;
      HTTPClient poll_http;
      String poll_url = cfg.api_url + "/device/claim-status/" + identity.claim_session_id;
      poll_http.begin(poll_client, poll_url);
      int poll_code = poll_http.GET();
      String poll_body = poll_http.getString();
      poll_http.end();

      Serial.printf("Polling: HTTP %d — %s\n", poll_code, poll_body.c_str());

      if (poll_code == 200) {
        JsonDocument poll_doc;
        DeserializationError poll_err = deserializeJson(poll_doc, poll_body);
        if (!poll_err) {
          String status = poll_doc["status"].as<String>();
          Serial.println("Polling: claim status = " + status);

          if (status == "active") {
            String resp_hardware_id = poll_doc["hardware_id"].as<String>();
            if (resp_hardware_id.length() == 0) {
              resp_hardware_id = poll_doc["uid"].as<String>();
            }
            String device_secret = poll_doc["device_secret"].as<String>();

            if (resp_hardware_id.length() > 0) {
              DeviceIdentity upd = identity_load();
              if (device_secret.length() > 0) {
                upd.device_secret = device_secret;
              }
              identity_save(upd);
              if (device_secret.length() > 0) {
                cfg.device_secret = device_secret;
              }
              active_without_hardware_id_count = 0;
              Serial.println("Polling: identity saved");
            } else {
              active_without_hardware_id_count++;
              if (active_without_hardware_id_count >= 3) {
                Serial.println("Polling: active but no hardware_id after 3 polls — exiting with partial claim");
                break;
              }
              Serial.println("Polling: WARNING — active but no hardware_id in response, retrying (" +
                String(active_without_hardware_id_count) + "/3)");
              continue;
            }

            if (cfg.device_secret.length() == 0) {
              active_without_secret_count++;
              if (active_without_secret_count >= 3) {
                Serial.println("Polling: active but no device_secret after 3 polls — exiting, secret rotation needed");
                break; // exit provisioning loop, device is claimed but needs secret rotation
              }
              Serial.println("Polling: WARNING — no device_secret yet, retrying (" +
                String(active_without_secret_count) + "/3)");
              continue;
            }
            active_without_secret_count = 0;

            Serial.println("Polling: device claimed — exiting provisioning");
            claim_done = true;
          } else if (status == "expired") {
            Serial.println("Polling: claim expired — showing expired screen");
            provisioning_screen_draw_claim_expired();
            Serial.println("Expired: hold KEY1 for 3s to restart provisioning");
            while (true) {
              if (digitalRead(KEY1_PIN) == LOW) {
                uint32_t press_start = millis();
                bool held = true;
                while (millis() - press_start < 3000) {
                  delay(100);
                  if (digitalRead(KEY1_PIN) != LOW) {
                    held = false;
                    break;
                  }
                }
                if (held) {
                  Serial.println("Expired: KEY1 held 3s — restarting");
                  ESP.restart();
                }
              }
              delay(100);
            }
          }
        }
      }
    }

    delay(10);
  }
}
