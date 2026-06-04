#include <Arduino.h>
#include <WiFi.h>
#include "device_identity.h"

String device_get_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}
