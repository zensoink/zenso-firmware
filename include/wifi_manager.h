#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include "config.h"

bool wifi_connect(const DeviceConfig &cfg, uint32_t timeout_ms = 15000);
void wifi_start_provisioning(DeviceConfig &cfg);

#endif
