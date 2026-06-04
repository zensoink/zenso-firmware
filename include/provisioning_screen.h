#ifndef PROVISIONING_SCREEN_H
#define PROVISIONING_SCREEN_H

#include <Arduino.h>

void provisioning_screen_draw(
  const String &ap_ssid,
  const String &ap_password,
  const String &ap_url,
  const String &firmware_version
);

#endif
