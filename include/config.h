#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define DEFAULT_REFRESH_RATE_S 300UL

struct DeviceConfig {
  String ssid;
  String password;
  String api_url;
  String hardware_id;
  String device_secret;
  String display_profile;
  int refresh_rate;
};

bool config_load(DeviceConfig &cfg);
bool config_save(const DeviceConfig &cfg);
bool config_is_provisioned(const DeviceConfig &cfg);

#endif
