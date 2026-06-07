#ifndef DEV_MENU_H
#define DEV_MENU_H

#include <Arduino.h>
#include "config.h"

void print_dev_menu();
void handle_dev_command(char cmd, DeviceConfig &cfg);

#endif
