#ifndef PROVISIONING_SCREEN_H
#define PROVISIONING_SCREEN_H

#include <Arduino.h>

void provisioning_screen_init();
void provisioning_screen_draw(
  const String &ap_ssid,
  const String &ap_password,
  const String &ap_url,
  const String &firmware_version
);

void provisioning_screen_draw_waiting(const String &claim_url, const String &wifi_ip);
void provisioning_screen_draw_claim_expired();
void provisioning_screen_draw_no_content(const String &device_uid);

#endif
