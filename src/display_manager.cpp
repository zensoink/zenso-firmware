#include "display_manager.h"
#include "pins.h"
#include <GxEPD2_BW.h>
#include <epd7c/GxEPD2_730c_GDEP073E01.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include <epd7c/GxEPD2_565c.h>
#include <epd3c/GxEPD2_420c.h>

DisplayManager& DisplayManager::instance() {
    static DisplayManager inst;
    return inst;
}

DisplayManager::DisplayManager() : _display(nullptr), _current_profile("") {}

DisplayManager::~DisplayManager() {
    _destroy_driver();
}

void DisplayManager::_destroy_driver() {
    if (_display != nullptr) {
        _display->powerOff();
        delete _display;
        _display = nullptr;
    }
}

void DisplayManager::_create_driver(const String& profile) {
    _destroy_driver();
    _current_profile = profile;

    if (profile == "acep_7in3") {
        _display = new GxEPD2_BW<GxEPD2_730c_ACeP_730, GxEPD2_730c_ACeP_730::HEIGHT>(
            GxEPD2_730c_ACeP_730(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
        );
    } else if (profile == "acep_5in65") {
        _display = new GxEPD2_BW<GxEPD2_565c, GxEPD2_565c::HEIGHT>(
            GxEPD2_565c(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
        );
    } else if (profile == "bwr_4in2") {
        _display = new GxEPD2_BW<GxEPD2_420c, GxEPD2_420c::HEIGHT>(
            GxEPD2_420c(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
        );
    } else if (profile == "mono_800x480" || profile == "custom") {
        _current_profile = profile;
        _display = new GxEPD2_BW<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT>(
            GxEPD2_730c_GDEP073E01(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
        );
    } else {
        // default / fallback: "spectra6_7in3"
        _current_profile = "spectra6_7in3";
        _display = new GxEPD2_BW<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT>(
            GxEPD2_730c_GDEP073E01(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
        );
    }
    Serial.printf("[DisplayManager] Instantiated driver for profile: %s (resolution: %dx%d)\n",
                  _current_profile.c_str(), _display->width(), _display->height());
}

void DisplayManager::init(const String& profile) {
    if (_display == nullptr || _current_profile != profile) {
        _create_driver(profile);
    }
}

GxEPD2_GFX* DisplayManager::get_display() {
    if (_display == nullptr) {
        _create_driver("spectra6_7in3");
    }
    return _display;
}

String DisplayManager::get_current_profile() const {
    return _current_profile;
}

bool DisplayManager::switch_profile(const String& new_profile) {
    if (new_profile.length() == 0 || new_profile == _current_profile) {
        return false;
    }
    Serial.printf("[DisplayManager] Switching profile: %s -> %s\n",
                  _current_profile.c_str(), new_profile.c_str());
    _create_driver(new_profile);
    return true;
}

void DisplayManager::power_off() {
    if (_display != nullptr) {
        _display->powerOff();
    }
}
