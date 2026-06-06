#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "config.h"
#include "version.h"
#include "provisioning_screen.h"
#include "device_identity.h"
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char PROVISIONING_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zenso Setup</title>
<style>
  body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto}
  label{display:block;margin:12px 0}
  input{width:100%;padding:8px;box-sizing:border-box;margin-top:4px}
  button{padding:10px 20px;font-size:16px;margin-top:12px}
</style>
</head>
<body>
<h2>Zenso Configuration</h2>
<form method="POST" action="/save">
  <label>WiFi SSID/Name<input name="ssid" type="text"></label>
  <label>Password<input name="password" type="password"></label>
  <label>API URL<input name="api_url" type="text" placeholder="http://192.168.1.x:3000"></label>
  <button type="submit">Save & Restart</button>
</form>
</body>
</html>
)rawliteral";


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
  String device_id = device_get_id();

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
  Serial.println("Provisioning: Device ID=" + device_id);
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
      server.send(302, "text/html",
        "<html><meta http-equiv='refresh' content='0;url=/'><body>"
        "<a href='/'>Continue to setup</a></body></html>");
    });
  }

  server.on("/", [&server]() {
    server.send(200, "text/html", PROVISIONING_HTML);
  });

  server.on("/save", HTTP_POST, [&cfg, &server]() {
    cfg.ssid = server.arg("ssid");
    cfg.password = server.arg("password");
    cfg.api_url = server.arg("api_url");

    config_save(cfg);
    Serial.println("Provisioning: config saved");

    if (!wifi_connect(cfg, 15000)) {
      Serial.println("Provisioning: WiFi connection failed");
      server.send(200, "text/html",
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Connection Failed</title>"
        "<style>"
        "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
        "button{padding:10px 20px;font-size:16px;margin-top:12px;background:#f44336;color:#fff;border:none;border-radius:4px;cursor:pointer}"
        "</style></head><body>"
        "<h2>WiFi Connection Failed</h2>"
        "<p>Could not connect to <strong>" + cfg.ssid + "</strong>.</p>"
        "<p>Check your WiFi credentials and try again.</p>"
        "<button onclick=\"window.location.href='/'\">Go Back</button>"
        "</body></html>");
      return;
    }

    Serial.printf("Provisioning: WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

    WiFiClient client;
    HTTPClient http;
    String bootstrap_url = cfg.api_url + "/device/bootstrap";
    http.begin(client, bootstrap_url);
    http.addHeader("Content-Type", "application/json");

    String device_id = device_get_id();
    JsonDocument body;
    body["device_id"] = device_id;
    body["local_setup_token"] = device_id;
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

    if (http_code != 200) {
      Serial.printf("Provisioning: bootstrap HTTP %d\n", http_code);
      server.send(200, "text/html",
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Server Error</title>"
        "<style>"
        "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
        "button{padding:10px 20px;font-size:16px;margin-top:12px;background:#f44336;color:#fff;border:none;border-radius:4px;cursor:pointer}"
        "</style></head><body>"
        "<h2>Server Error</h2>"
        "<p>Failed to register device with server.</p>"
        "<p>HTTP status: " + String(http_code) + "</p>"
        "<button onclick=\"window.location.href='/'\">Go Back</button>"
        "</body></html>");
      return;
    }

    JsonDocument resp;
    DeserializationError json_err = deserializeJson(resp, response_body);
    if (json_err || !resp["claim_url"].is<String>()) {
      Serial.printf("Provisioning: bootstrap JSON parse error: %s\n", json_err.c_str());
      server.send(200, "text/html",
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Parse Error</title>"
        "<style>"
        "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
        "button{padding:10px 20px;font-size:16px;margin-top:12px;background:#f44336;color:#fff;border:none;border-radius:4px;cursor:pointer}"
        "</style></head><body>"
        "<h2>Invalid Server Response</h2>"
        "<button onclick=\"window.location.href='/'\">Go Back</button>"
        "</body></html>");
      return;
    }

    String claim_url = resp["claim_url"].as<String>();
    String claim_expires_at = resp["claim_expires_at"].as<String>();

    String success_page =
      "<!DOCTYPE html><html><head>"
      "<meta charset=\"UTF-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Setup Complete</title>"
      "<style>"
      "body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}"
      "a{word-break:break-all;color:#1976D2}"
      "button{padding:12px 24px;font-size:18px;margin-top:16px;background:#4CAF50;color:#fff;border:none;border-radius:4px;cursor:pointer}"
      "</style></head><body>"
      "<h2>WiFi Connected</h2>"
      "<p>Device connected to <strong>" + cfg.ssid + "</strong>.</p>"
      "<p>IP: " + WiFi.localIP().toString() + "</p>"
      "<hr>"
      "<p><strong>Claim your device:</strong></p>"
      "<p><a href=\"" + claim_url + "\">" + claim_url + "</a></p>"
      "<p>Expires: " + claim_expires_at + "</p>"
      "<button onclick=\"window.location.href='" + claim_url + "'\">Continue in Zenso</button>"
      "</body></html>";

    server.send(200, "text/html", success_page);
    Serial.println("Provisioning: bootstrap success, portal continues");
  });

  server.onNotFound([&server]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/html",
      "<html><meta http-equiv='refresh' content='0;url=/'><body>"
      "<a href='/'>Continue to setup</a></body></html>");
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

  while (true) {
    dns.processNextRequest();
    server.handleClient();
    delay(10);
  }
}
