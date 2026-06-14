#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "config.h"
#include "version.h"
#include "provisioning_screen.h"
#include "device_identity.h"
#include "portal_html.h"
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


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

    claim_url = resp["claim_url"].as<String>();
    String claim_session_id = resp["claim_session_id"].as<String>();
    String claim_expires_at = resp["claim_expires_at"].as<String>();

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

  while (!claim_done) {
    dns.processNextRequest();
    server.handleClient();

    uint32_t now = millis();
    if (now - last_poll >= POLL_INTERVAL_MS) {
      last_poll = now;

      // Load claim_session_id from identity
      DeviceIdentity identity = identity_load();
      if (identity.claim_session_id.length() == 0) {
        Serial.println("Polling: no claim_session_id, skipping");
        delay(10);
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
            String device_secret = poll_doc["device_secret"].as<String>();

            if (resp_hardware_id.length() > 0) {
              DeviceIdentity upd = identity_load();
              upd.hardware_id = normalize_hardware_id(resp_hardware_id);
              if (device_secret.length() > 0) {
                upd.device_secret = device_secret;
              }
              identity_save(upd);
              cfg.hardware_id = upd.hardware_id;
              if (device_secret.length() > 0) {
                cfg.device_secret = device_secret;
              }
              Serial.println("Polling: identity saved — hardware_id=" + upd.hardware_id);
            } else {
              Serial.println("Polling: WARNING — active but no hardware_id in response, retrying next poll");
              delay(10);
              continue;
            }

            if (cfg.device_secret.length() == 0) {
              Serial.println("Polling: WARNING — no device_secret yet, retrying");
              delay(10);
              continue;
            }

            Serial.println("Polling: device claimed — exiting provisioning");
            claim_done = true;
          } else if (status == "expired") {
            Serial.println("Polling: claim expired — showing expired screen");
            provisioning_screen_draw_claim_expired();
            while (true) { delay(1000); }
          }
        }
      }
    }

    delay(10);
  }
}
