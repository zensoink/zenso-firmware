#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <Arduino.h>
#include "config.h"
#include "ota_manager.h"

enum class FetchResult {
  OK,           // 200 — success
  NOT_MODIFIED, // 304 — cache still valid
  NO_CONTENT,   // 404 — device has no screen assigned
  ERROR         // any other failure
};

struct DeviceStatus {
  int screen_id;       // -1 = none
  int refresh_rate;    // seconds
  bool has_image;
  bool content_changed;
  String display_profile;
  int width;
  int height;
  bool has_ota = false;
  OtaDirective ota;
};

FetchResult http_fetch_display(const DeviceConfig &cfg);
bool device_login(const DeviceConfig &cfg, String &out_token);
FetchResult http_check_in(
  const DeviceConfig &cfg,
  const String &token,
  DeviceStatus &out_status,
  const String &ota_status = "",
  const String &ota_error = ""
);
FetchResult http_fetch_display_with_token(const DeviceConfig &cfg, const String &token);

#endif
