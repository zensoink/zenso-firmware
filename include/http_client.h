#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <Arduino.h>
#include "config.h"

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
};

FetchResult http_fetch_display(const DeviceConfig &cfg);
bool device_login(const DeviceConfig &cfg, String &out_token);
FetchResult http_check_in(const DeviceConfig &cfg, const String &token, DeviceStatus &out_status);
FetchResult http_fetch_display_with_token(const DeviceConfig &cfg, const String &token);

#endif
