#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

#ifndef ENABLE_GxEPD2_GFX
#define ENABLE_GxEPD2_GFX 1
#endif

#include <GxEPD2_GFX.h>

class DisplayManager {
public:
    static DisplayManager& instance();

    // Initialize or re-initialize display for given profile
    void init(const String& profile);

    // Get current display GFX pointer
    GxEPD2_GFX* get_display();

    // Get active profile identifier string
    String get_current_profile() const;

    // Switch profile dynamically (destroys old driver, instantiates new)
    bool switch_profile(const String& new_profile);

    // Turn off display power
    void power_off();

private:
    DisplayManager();
    ~DisplayManager();
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    void _create_driver(const String& profile);
    void _destroy_driver();

    GxEPD2_GFX* _display;
    String _current_profile;
};

#endif // DISPLAY_MANAGER_H
