#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "config.h"

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
<h2>Zenso WiFi Setup</h2>
<form method="POST" action="/save">
  <label>WiFi SSID<input name="ssid" type="text"></label>
  <label>Password<input name="password" type="password"></label>
  <label>API URL<input name="api_url" type="text" placeholder="http://192.168.1.x:3000"></label>
  <label>Device ID<input name="device_id" type="text" placeholder="device-001"></label>
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

bool wifi_connect(const DeviceConfig &cfg, uint32_t timeout_ms) {
  Serial.printf("WiFi: connecting to %s...\n", cfg.ssid.c_str());

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

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Zenso-Setup");
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("Provisioning: AP started, IP: %s\n", apIP.toString().c_str());

  DNSServer dns;
  dns.start(53, "*", apIP);

  WebServer server(80);

  server.on("/", [&server]() {
    server.send(200, "text/html", PROVISIONING_HTML);
  });

  server.on("/save", HTTP_POST, [&cfg, &server]() {
    cfg.ssid = server.arg("ssid");
    cfg.password = server.arg("password");
    cfg.api_url = server.arg("api_url");
    cfg.device_id = server.arg("device_id");

    server.send(200, "text/html", SUCCESS_HTML);

    if (config_save(cfg)) {
      Serial.println("Provisioning: config saved, restarting...");
    } else {
      Serial.println("Provisioning: config save failed, restarting anyway...");
    }
    delay(1000);
    ESP.restart();
  });

  server.onNotFound([&apIP, &server]() {
    server.sendHeader("Location", "http://" + apIP.toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Provisioning: HTTP server started on port 80");
  Serial.println("Provisioning: connect to Zenso-Setup and open http://192.168.4.1");

  while (true) {
    dns.processNextRequest();
    server.handleClient();
    delay(10);
  }
}
