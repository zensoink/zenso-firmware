#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include <qrcode.h>
#include "pins.h"
#include "provisioning_screen.h"

#ifndef MAX_DISPLAY_BUFFER_SIZE
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#endif

#ifndef MAX_HEIGHT_7C
#define MAX_HEIGHT_7C(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2)) ? \
                            EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))
#endif

extern GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)> display;

void provisioning_screen_init() {
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();
}

void provisioning_screen_draw(
  const String &ap_ssid,
  const String &ap_password,
  const String &ap_url,
  const String &firmware_version)
{
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ---- QR code (WPA credentials) ----
    String wifi_qr = "WIFI:T:WPA;S:" + ap_ssid + ";P:" + ap_password + ";;";

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];

    if (qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, wifi_qr.c_str()) == 0) {
      const uint8_t module_px = 5;
      uint16_t qr_size = qrcode.size * module_px;
      int16_t qr_x = 40;
      int16_t qr_y = 60;

      // White background with quiet zone
      display.fillRect(qr_x - module_px, qr_y - module_px,
                       qr_size + 2 * module_px, qr_size + 2 * module_px, GxEPD_WHITE);

      // Draw QR modules
      for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
          if (qrcode_getModule(&qrcode, x, y)) {
            display.fillRect(qr_x + x * module_px, qr_y + y * module_px,
                             module_px, module_px, GxEPD_BLACK);
          }
        }
      }
    }

    // ---- Text ----
    display.setTextColor(GxEPD_BLACK);

    // Line 1: Title
    display.setCursor(300, 80);
    display.setTextSize(3);
    display.print("Setup Mode");

    // Line 2: Step 1
    display.setCursor(300, 140);
    display.setTextSize(2);
    display.print("1. Scan QR or connect to WiFi:");

    // Line 3: AP SSID
    display.setCursor(300, 170);
    display.setTextSize(2);
    display.print("   SSID: ");
    display.print(ap_ssid);

    // Line 4: Password
    display.setCursor(300, 200);
    display.setTextSize(2);
    display.print("   Password: ");
    display.print(ap_password);

    // Line 5: Step 2
    display.setCursor(300, 250);
    display.setTextSize(2);
    display.print("2. Open in browser:");

    // Line 6: AP URL
    display.setCursor(300, 280);
    display.setTextSize(2);
    display.print("   ");
    display.print(ap_url);

    // ---- Bottom: Firmware version (right-aligned) ----
    String fw_str = "Firmware: " + firmware_version;
    int16_t x1, y1;
    uint16_t fw_w, fw_h;
    display.setTextSize(1);
    display.getTextBounds(fw_str, 0, 0, &x1, &y1, &fw_w, &fw_h);
    display.setCursor(760 - fw_w, 435);
    display.print(fw_str);

  } while (display.nextPage());

  display.powerOff();
}
