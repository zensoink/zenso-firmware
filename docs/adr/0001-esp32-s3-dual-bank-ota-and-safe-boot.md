# ESP32-S3 Dual-Bank OTA Flashing and Safe Boot Rollback

For physical Zenso e-paper displays running on the Seeed XIAO ESP32-S3, remote updates must be resilient against corrupted downloads, bricked boots, and power cuts during long sleep cycles. We decided to implement synchronous dual-bank OTA flashing using native ESP-IDF partition streaming (`esp_ota_*`) with hardware SHA-256 verification, coupled with a strict Safe Boot health check that cancels rollback only after successful WiFi connection and backend check-in.

## Consequences

- The device uses standard `default_8MB.csv` partition table with two 3.25MB app slots (`app0` / `app1`) and dedicated `otadata`.
- Before streaming starts, the display renders a static "Updating Firmware..." notice and powers down the display controller to prevent SPI bus bus-contention and display refresh artifacts during reboot.
- Flashing runs synchronously during the wake-up cycle in `setup()`, directly suppressing deep sleep until the binary is either flashed or rejected.
- If download or checksum verification fails, the device restores the screen from cached `/display.raw`, reports `otaStatus: failed` with error telemetry to `POST /devices/check-in`, and enters scheduled sleep.
- Newly booted firmware runs in `PENDING_VERIFY` state: it retries network association up to 3 times while awake; if check-in fails, it rolls back automatically via `esp_ota_mark_app_invalid_rollback_and_reboot()`.
- State transitions and error telemetry are persisted across reboots in NVS under the `zenso_ota` namespace.
