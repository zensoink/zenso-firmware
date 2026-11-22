#include <Arduino.h>
#include <SPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <memory>

#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <LittleFS.h>

#include "ota_manager.h"
#include "http_client.h"
#include "provisioning_screen.h"
#include "display_manager.h"
#include "device_identity.h"
#include "version.h"

static const char *PREFS_NAMESPACE = "zenso_ota";
static bool s_is_pending_verify = false;

// ── Helpers ──

static WiFiClient* make_ota_client(const String &url) {
  if (url.startsWith("https://")) {
    auto *c = new WiFiClientSecure();
    c->setInsecure();
    return c;
  }
  return new WiFiClient();
}

static void report_ota_failure(
  const DeviceConfig &cfg,
  const String &token,
  const String &fail_reason
) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("rep_status", "failed");
  prefs.putString("rep_error", fail_reason);
  prefs.putBool("pending_verify", false);
  prefs.end();

  DeviceStatus dummy_status;
  http_check_in(cfg, token, dummy_status, "failed", fail_reason);
}

String ota_bytes_to_hex(const uint8_t *bytes, size_t len) {
  String hex_str;
  hex_str.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", bytes[i]);
    hex_str += buf;
  }
  return hex_str;
}

bool ota_validate_checksum(const String &expected_hex, const uint8_t *digest_bytes) {
  if (expected_hex.length() != 64) {
    return false;
  }
  String actual_hex = ota_bytes_to_hex(digest_bytes, 32);
  if (expected_hex.length() != actual_hex.length()) {
    return false;
  }
  for (size_t i = 0; i < 64; ++i) {
    if (tolower(expected_hex[i]) != tolower(actual_hex[i])) {
      return false;
    }
  }
  return true;
}

// ── Lifecycle & Safe Boot ──

void ota_manager_init() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  bool was_pending = prefs.getBool("pending_verify", false);
  String target_ver = prefs.getString("target_ver", "");

  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr) {
    esp_ota_get_state_partition(running, &state);
  }

  Serial.println("\n════════════════════════════════════════════════");
  Serial.printf("[OTA] Running partition: %s (address 0x%08x)\n",
                running ? running->label : "unknown",
                running ? (unsigned int)running->address : 0);
  Serial.printf("[OTA] State: %d, NVS was_pending=%d, target_ver='%s'\n",
                (int)state, was_pending ? 1 : 0, target_ver.c_str());

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    s_is_pending_verify = true;
    Serial.printf("[OTA] Booted in PENDING_VERIFY mode for v%s. Must validate health before deep sleep!\n",
                  FIRMWARE_VERSION);
  } else if (was_pending) {
    // We were expecting target_ver to boot and verify, but we did not boot in PENDING_VERIFY.
    // Bootloader automatically rolled back to this partition!
    Serial.printf("[OTA] Rollback detected! Target v%s failed to boot. Active firmware remains v%s.\n",
                  target_ver.c_str(), FIRMWARE_VERSION);
    prefs.putString("rep_status", "failed");
    prefs.putString("rep_error", "Firmware rollback: target version " + target_ver + " failed boot health check");
    prefs.putBool("pending_verify", false);
  }
  Serial.println("════════════════════════════════════════════════\n");

  prefs.end();
}

bool ota_manager_is_pending_verify() {
  return s_is_pending_verify;
}

bool ota_manager_has_pending_report(String &out_status, String &out_error) {
  if (s_is_pending_verify) {
    out_status = "success";
    out_error = "";
    return true;
  }
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, true);
  out_status = prefs.getString("rep_status", "");
  out_error = prefs.getString("rep_error", "");
  prefs.end();
  return out_status.length() > 0;
}

void ota_manager_clear_pending_report() {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.remove("rep_status");
  prefs.remove("rep_error");
  prefs.end();
  Serial.println("[OTA] Pending status report cleared from NVS.");
}

bool ota_manager_mark_app_valid() {
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    Serial.println("\n════════════════════════════════════════════════");
    Serial.println("[OTA] Firmware validated successfully! Rollback cancelled.");
    Serial.println("════════════════════════════════════════════════\n");
    s_is_pending_verify = false;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool("pending_verify", false);
    prefs.putString("rep_status", "success");
    prefs.putString("rep_error", "");
    prefs.remove("target_ver");
    prefs.end();
    return true;
  }

  Serial.printf("[OTA] Failed to mark app valid: esp_err=0x%x\n", err);
  return false;
}

void ota_manager_rollback() {
  Serial.println("\n════════════════════════════════════════════════");
  Serial.println("[OTA] Explicit rollback requested: marking app invalid and rebooting!");
  Serial.println("════════════════════════════════════════════════\n");

  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("rep_status", "failed");
  prefs.putString("rep_error", "Boot health check failed: WiFi/API unreachable after 3 attempts");
  prefs.putBool("pending_verify", false);
  prefs.end();

  esp_ota_mark_app_invalid_rollback_and_reboot();
}

// ── Streaming Download & Flash ──

bool ota_manager_execute(
  const DeviceConfig &cfg,
  const String &token,
  const OtaDirective &directive
) {
  if (directive.version == FIRMWARE_VERSION) {
    Serial.printf("[OTA] Target version %s matches currently installed firmware. Skipping update.\n",
                  directive.version.c_str());
    return true;
  }

  Serial.println("\n════════════════════════════════════════════════");
  Serial.printf("[OTA] Beginning firmware update to version %s\n", directive.version.c_str());
  Serial.printf("[OTA] Target download URL: %s%s\n", cfg.api_url.c_str(), directive.url.c_str());
  Serial.printf("[OTA] Expected SHA-256: %s\n", directive.checksum_sha256.c_str());
  if (directive.size_bytes > 0) {
    Serial.printf("[OTA] Expected Binary Size: %u bytes\n", (uint32_t)directive.size_bytes);
  }
  Serial.println("════════════════════════════════════════════════");

  // Step 1: Render Updating screen and power off panel controller
  Serial.println("[OTA] Rendering 'Updating Firmware' notification on e-paper...");
  provisioning_screen_draw_updating(directive.version);
  DisplayManager::instance().power_off();
  Serial.println("[OTA] E-paper controller powered off for safe flash operation.");

  // Step 2: Resolve inactive target flash partition
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(nullptr);
  if (!update_partition) {
    Serial.println("[OTA] ERROR: No inactive OTA partition found!");
    report_ota_failure(cfg, token, "No inactive OTA partition found");
    return false;
  }
  Serial.printf("[OTA] Target flash partition: %s (0x%08x, size: %u bytes)\n",
                update_partition->label,
                (uint32_t)update_partition->address,
                (uint32_t)update_partition->size);

  // Step 3: Connect to firmware proxy first
  String download_url = cfg.api_url + directive.url;
  std::unique_ptr<WiFiClient> client(make_ota_client(download_url));
  client->setTimeout(30);

  HTTPClient http;
  http.setTimeout(30000);
  http.begin(*client, download_url);
  http.addHeader("Authorization", "Bearer " + token);

  Serial.println("[OTA] Connecting to firmware proxy...");
  int32_t http_code = http.GET();
  if (http_code != 200) {
    Serial.printf("[OTA] ERROR: Firmware download failed: HTTP %d (%s)\n",
                  (int)http_code, http.errorToString(http_code).c_str());
    http.end();
    report_ota_failure(cfg, token, "Firmware download HTTP " + String(http_code));
    return false;
  }

  int32_t content_length = http.getSize();
  if (directive.size_bytes > 0 && content_length > 0 && (size_t)content_length != directive.size_bytes) {
    Serial.printf("[OTA] ERROR: Size mismatch! Expected %u, got %d bytes\n",
                  (uint32_t)directive.size_bytes, (int)content_length);
    http.end();
    report_ota_failure(cfg, token, "Size mismatch: expected " + String(directive.size_bytes) + " got " + String(content_length));
    return false;
  }

  // Step 4: Begin OTA partition write and initialize SHA-256 context
  size_t ota_size = content_length > 0 ? (size_t)content_length : (directive.size_bytes > 0 ? directive.size_bytes : OTA_SIZE_UNKNOWN);
  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, ota_size, &ota_handle);
  if (err != ESP_OK) {
    Serial.printf("[OTA] ERROR: esp_ota_begin failed: 0x%x\n", err);
    http.end();
    report_ota_failure(cfg, token, "esp_ota_begin failed 0x" + String(err, HEX));
    return false;
  }

  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0); // 0 = SHA-256

  Serial.printf("[OTA] Streaming binary, Content-Length: %d bytes (free heap: %u)\n",
                (int)content_length, (uint32_t)ESP.getFreeHeap());

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t total_written = 0;
  uint32_t last_read_ms = millis();
  uint32_t last_progress_ms = millis();
  bool download_failed = false;
  String fail_reason;

  while (content_length > 0 ? (total_written < (size_t)content_length) : (http.connected() || stream->available())) {
    size_t available = stream->available();
    if (available > 0) {
      size_t to_read = available < sizeof(buf) ? available : sizeof(buf);
      if (content_length > 0 && ((size_t)content_length - total_written) < to_read) {
        to_read = (size_t)content_length - total_written;
      }
      int len = stream->read(buf, to_read);
      if (len > 0) {
        mbedtls_sha256_update(&sha_ctx, buf, (size_t)len);
        err = esp_ota_write(ota_handle, buf, (size_t)len);
        if (err != ESP_OK) {
          download_failed = true;
          fail_reason = "esp_ota_write failed (error 0x" + String(err, HEX) + ") at byte " + String(total_written);
          Serial.printf("[OTA] ERROR: %s\n", fail_reason.c_str());
          break;
        }
        total_written += (size_t)len;
        last_read_ms = millis();
      }
    } else {
      if (millis() - last_read_ms > 20000) {
        download_failed = true;
        fail_reason = "Stream timeout: no data received for 20s";
        Serial.printf("[OTA] ERROR: %s\n", fail_reason.c_str());
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (millis() - last_progress_ms > 1000 && total_written > 0) {
      last_progress_ms = millis();
      if (content_length > 0) {
        Serial.printf("[OTA] Flashed %u / %d bytes (%u%%)...\n",
                      (uint32_t)total_written, (int)content_length,
                      (uint32_t)(total_written * 100 / (size_t)content_length));
      } else {
        Serial.printf("[OTA] Flashed %u bytes...\n", (uint32_t)total_written);
      }
    }
  }

  http.end();

  // Step 5: Finalize SHA-256 calculation
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha_ctx, digest);
  mbedtls_sha256_free(&sha_ctx);

  String computed_hex = ota_bytes_to_hex(digest, 32);
  Serial.printf("[OTA] Calculated SHA-256: %s\n", computed_hex.c_str());
  Serial.printf("[OTA] Expected   SHA-256: %s\n", directive.checksum_sha256.c_str());

  if (!download_failed && !ota_validate_checksum(directive.checksum_sha256, digest)) {
    download_failed = true;
    fail_reason = "SHA-256 mismatch: expected " + directive.checksum_sha256 + ", got " + computed_hex;
    Serial.printf("[OTA] ERROR: %s\n", fail_reason.c_str());
  }

  // Step 6: Handle failure or commit
  if (download_failed) {
    esp_ota_abort(ota_handle);
    Serial.println("[OTA] Aborted flash write. Active partition preserved.");
    report_ota_failure(cfg, token, fail_reason);
    return false;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    Serial.printf("[OTA] ERROR: esp_ota_end failed: 0x%x\n", err);
    report_ota_failure(cfg, token, "esp_ota_end failed 0x" + String(err, HEX));
    return false;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    Serial.printf("[OTA] ERROR: esp_ota_set_boot_partition failed: 0x%x\n", err);
    report_ota_failure(cfg, token, "esp_ota_set_boot_partition failed 0x" + String(err, HEX));
    return false;
  }

  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString("target_ver", directive.version);
  prefs.putBool("pending_verify", true);
  prefs.putString("rep_status", "updating");
  prefs.remove("rep_error");
  prefs.end();

  Serial.println("\n════════════════════════════════════════════════");
  Serial.printf("[OTA] SUCCESS! Flashed v%s to partition '%s'.\n",
                directive.version.c_str(), update_partition->label);
  Serial.println("[OTA] Restarting device to boot into new firmware...");
  Serial.println("════════════════════════════════════════════════\n");

  delay(1000);
  ESP.restart();
  return true;
}
