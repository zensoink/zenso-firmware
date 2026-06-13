#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include "dev_menu.h"
#include "provisioning_screen.h"
#include "version.h"
#include "pins.h"
#include "device_identity.h"

#ifndef MAX_DISPLAY_BUFFER_SIZE
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#endif

#ifndef MAX_HEIGHT_7C
#define MAX_HEIGHT_7C(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2)) ? \
                            EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))
#endif

extern GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)> display;

extern bool drawRAW(const char *filename);
extern void clearToWhite();

void print_dev_menu() {
  Serial.println();
  Serial.println("══════════ Developer Menu ══════════");
  Serial.println("  h  — show this menu");
  Serial.println("  c  — clear screen to white");
  Serial.println("  r  — re-render /display.raw");
  Serial.println("  1  — show 'Setup Mode' screen");
  Serial.println("  2  — show 'Waiting' screen");
  Serial.println("  3  — show 'Claim Expired' screen");
  Serial.println("  4  — show 'No Content' screen");
  Serial.println("  esc — clear screen");
  Serial.println("════════════════════════════════════");
}

void handle_dev_command(char cmd, DeviceConfig &cfg) {
  if (cmd == 'h' || cmd == 'H') {
    print_dev_menu();
    return;
  }

  if (cmd == 27 || cmd == 'c' || cmd == 'C') {
    clearToWhite();
    return;
  }

  if (cmd == 'r' || cmd == 'R') {
    Serial.println("Re-rendering RAW...");
    SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
    display.init(115200, true, 2, false);
    display.setRotation(0);
    display.setFullWindow();
    drawRAW("/display.raw");
    display.powerOff();
    return;
  }

  if (cmd == '1') {
    provisioning_screen_init();
    provisioning_screen_draw("Zenso-Setup", "devpassword", "http://192.168.4.1", FIRMWARE_VERSION);
    Serial.println("-> Setup screen shown");
    return;
  }

  if (cmd == '2') {
    provisioning_screen_init();
    provisioning_screen_draw_waiting("http://zenso.ink/claim/DEV", WiFi.localIP().toString());
    Serial.println("-> Waiting screen shown");
    return;
  }

  if (cmd == '3') {
    provisioning_screen_init();
    provisioning_screen_draw_claim_expired();
    Serial.println("-> Expired screen shown");
    return;
  }

  if (cmd == '4') {
    provisioning_screen_init();
    provisioning_screen_draw_no_content(cfg.hardware_id);
    Serial.println("-> No-content screen shown");
    return;
  }
}
