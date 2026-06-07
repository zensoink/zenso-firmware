#pragma once

#include <Arduino.h>

struct DeviceIdentity {
  String device_id;
  String bootstrap_secret;
  String claim_session_id;
  String claim_url;
  String uid;
  String device_secret;
};

String device_get_id();
DeviceIdentity identity_load();
void identity_save(const DeviceIdentity &identity);
