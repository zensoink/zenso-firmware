#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <Arduino.h>
#include "config.h"

enum class FetchResult {
  OK,           // 200 — new content downloaded
  NOT_MODIFIED, // 304 — cache still valid
  NO_CONTENT,   // 404 — device has no screen assigned
  ERROR         // any other failure (no wifi, auth fail, parse fail, etc.)
};

FetchResult http_fetch_display(const DeviceConfig &cfg);

#endif
