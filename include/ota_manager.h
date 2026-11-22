#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include "config.h"

struct OtaDirective {
  String version;
  String url;
  String checksum_sha256;
  size_t size_bytes = 0;
};

void ota_manager_init();
bool ota_manager_is_pending_verify();
bool ota_manager_has_pending_report(String &out_status, String &out_error);
void ota_manager_clear_pending_report();
bool ota_manager_mark_app_valid();
void ota_manager_rollback();

bool ota_manager_execute(
  const DeviceConfig &cfg,
  const String &token,
  const OtaDirective &directive
);

String ota_bytes_to_hex(const uint8_t *bytes, size_t len);
bool ota_validate_checksum(const String &expected_hex, const uint8_t *digest_bytes);

#endif
