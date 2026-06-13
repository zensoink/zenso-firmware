#pragma once

#include <Arduino.h>

struct DeviceIdentity {
  String hardware_id;
  String bootstrap_secret;
  String claim_session_id;
  String claim_url;
  String device_secret;
};

String device_get_id();
String normalize_hardware_id(const String &raw);
DeviceIdentity identity_load();
void identity_save(const DeviceIdentity &identity);
