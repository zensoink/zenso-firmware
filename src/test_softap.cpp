#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_AP);
  delay(100);

  bool ok = WiFi.softAP("Zenso-Setup");
  delay(500);

  Serial.printf("softAP result: %s\n", ok ? "true" : "false");
  Serial.printf("WiFi mode: %d\n", WiFi.getMode());
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {}
