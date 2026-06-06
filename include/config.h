#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

struct DeviceConfig {
  String ssid;
  String password;
  String api_url;
  String device_uid;
  String device_secret;
};

bool config_load(DeviceConfig &cfg);
bool config_save(const DeviceConfig &cfg);
bool config_is_provisioned(const DeviceConfig &cfg);

#endif
