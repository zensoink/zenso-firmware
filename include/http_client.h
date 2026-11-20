#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <Arduino.h>
#include "config.h"

enum class FetchResult {
  OK,           // 200 — success
  NOT_MODIFIED, // 304 — cache still valid
  NO_CONTENT,   // 404 — device has no screen assigned
  SERVER_ERROR, // 5xx — server error for exponential backoff
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
};

FetchResult http_fetch_display(const DeviceConfig &cfg);
bool device_login(const DeviceConfig &cfg, String &out_token);
FetchResult http_check_in(const DeviceConfig &cfg, const String &token, DeviceStatus &out_status, const char* wakeup_reason = "boot", const char* display_state = "active");
FetchResult http_fetch_display_with_token(const DeviceConfig &cfg, const String &token, char* rtc_etag = nullptr, size_t rtc_etag_len = 0);

#endif
