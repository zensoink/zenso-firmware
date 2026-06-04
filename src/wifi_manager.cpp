#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "config.h"
#include "version.h"
#include "provisioning_screen.h"
#include "device_identity.h"

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

static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zenso Setup</title>
<style>
  body{font-family:sans-serif;padding:20px;max-width:400px;margin:0 auto;text-align:center}
</style>
</head>
<body>
<h2>Configuration Saved</h2>
<p>The device will now restart and connect to your WiFi network.</p>
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
  WiFi.mode(WIFI_STA);
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

  WiFi.mode(WIFI_AP);
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

    server.send(200, "text/html", SUCCESS_HTML);

    if (config_save(cfg)) {
      Serial.println("Provisioning: config saved, restarting...");
    } else {
      Serial.println("Provisioning: config save failed, restarting anyway...");
    }
    delay(1000);
    ESP.restart();
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
