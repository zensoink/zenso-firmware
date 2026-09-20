#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Deep-sleep interval until the backend owns the sleep schedule (15 min).
// Backend task: return sleep/wake schedule in device check-in response.
#define DEEP_SLEEP_INTERVAL_MS (15UL * 60UL * 1000UL)

struct DeviceConfig {
  String ssid;
  String password;
  String api_url;
  String hardware_id;
  String device_secret;
  String display_profile;
};

bool config_load(DeviceConfig &cfg);
bool config_save(const DeviceConfig &cfg);
bool config_is_provisioned(const DeviceConfig &cfg);

#endif
