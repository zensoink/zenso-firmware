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

// ── Layout constants (display: 800×480) ──

// Left-column x (text-only screens)
static const int16_t LX = 55;

// Right-column x (screens with QR + text side by side)
static const int16_t RIGHT_X   = 310;
static const int16_t QR_X      = 35;
static const int16_t QR_Y      = 50;
static const int16_t QR_MODULE = 5;

// Left-column Y positions (text-only screens)
static const int16_t Y_TITLE = 100;
static const int16_t Y_BODY1 = 160;
static const int16_t Y_BODY2 = 200;
static const int16_t Y_BODY3 = 240;
static const int16_t Y_BODY4 = 290;

// Footer / branding (shared by all)
static const int16_t BRANDING_X = 700;
static const int16_t BRANDING_Y = 455;

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
      uint16_t qr_size = qrcode.size * QR_MODULE;

      display.fillRect(QR_X - QR_MODULE, QR_Y - QR_MODULE,
                       qr_size + 2 * QR_MODULE, qr_size + 2 * QR_MODULE, GxEPD_WHITE);

      for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
          if (qrcode_getModule(&qrcode, x, y)) {
            display.fillRect(QR_X + x * QR_MODULE, QR_Y + y * QR_MODULE,
                             QR_MODULE, QR_MODULE, GxEPD_BLACK);
          }
        }
      }
    }

    // ---- Text ----
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(RIGHT_X, 80);
    display.setTextSize(3);
    display.print("Setup Mode");

    display.setCursor(RIGHT_X, 140);
    display.setTextSize(2);
    display.print("1. Scan QR or connect to WiFi:");

    display.setCursor(RIGHT_X, 170);
    display.setTextSize(2);
    display.print("   SSID: ");
    display.print(ap_ssid);

    display.setCursor(RIGHT_X, 200);
    display.setTextSize(2);
    display.print("   Password: ");
    display.print(ap_password);

    display.setCursor(RIGHT_X, 250);
    display.setTextSize(2);
    display.print("2. Open in browser:");

    display.setCursor(RIGHT_X, 280);
    display.setTextSize(2);
    display.print("   ");
    display.print(ap_url);

    // Bottom: Firmware version + branding
    String fw_str = "Firmware: " + firmware_version;
    int16_t x1, y1;
    uint16_t fw_w, fw_h;
    display.setTextSize(1);
    display.getTextBounds(fw_str, 0, 0, &x1, &y1, &fw_w, &fw_h);
    display.setCursor(BRANDING_X - fw_w - 20, BRANDING_Y);
    display.print(fw_str);

    display.setCursor(BRANDING_X, BRANDING_Y);
    display.print("zenso.ink");

  } while (display.nextPage());

  display.powerOff();
}

void provisioning_screen_draw_waiting(const String &claim_url, const String &wifi_ip) {
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // ---- QR code (claim URL) ----
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(4)];

    if (qrcode_initText(&qrcode, qrcodeData, 4, ECC_LOW, claim_url.c_str()) == 0) {
      uint16_t qr_size = qrcode.size * QR_MODULE;

      display.fillRect(QR_X - QR_MODULE, QR_Y - QR_MODULE,
                       qr_size + 2 * QR_MODULE, qr_size + 2 * QR_MODULE, GxEPD_WHITE);

      for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
          if (qrcode_getModule(&qrcode, x, y)) {
            display.fillRect(QR_X + x * QR_MODULE, QR_Y + y * QR_MODULE,
                             QR_MODULE, QR_MODULE, GxEPD_BLACK);
          }
        }
      }
    }

    // ---- Text ----
    display.setTextColor(GxEPD_BLACK);

    display.setCursor(RIGHT_X, 60);
    display.setTextSize(3);
    display.print("Waiting for");

    display.setCursor(RIGHT_X, 100);
    display.setTextSize(3);
    display.print("Authorization");

    display.setCursor(RIGHT_X, 160);
    display.setTextSize(2);
    display.print("WiFi connected: ");
    display.print(wifi_ip);

    display.setCursor(RIGHT_X, 200);
    display.setTextSize(2);
    display.print("Scan QR or open link in browser");

    display.setCursor(RIGHT_X, 240);
    display.setTextSize(2);
    display.print("to claim this device");

    display.setCursor(BRANDING_X, BRANDING_Y);
    display.setTextSize(1);
    display.print("zenso.ink");

  } while (display.nextPage());

  display.powerOff();
}

void provisioning_screen_draw_claim_expired() {
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_RED);
    display.setCursor(LX, Y_TITLE);
    display.setTextSize(3);
    display.print("Claim Expired");

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(LX, Y_BODY1);
    display.setTextSize(2);
    display.print("Authorization window has closed.");

    display.setCursor(LX, Y_BODY2);
    display.setTextSize(2);
    display.print("To try again:");

    display.setCursor(LX, Y_BODY3);
    display.setTextSize(2);
    display.print("Hold KEY1 for 3 seconds");

    display.setCursor(LX, Y_BODY4);
    display.setTextSize(2);
    display.print("to restart provisioning.");

    display.setCursor(BRANDING_X, BRANDING_Y);
    display.setTextSize(1);
    display.print("zenso.ink");

  } while (display.nextPage());

  display.powerOff();
}

void provisioning_screen_draw_no_content(const String &device_uid) {
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_GREEN);
    display.setCursor(LX, Y_TITLE);
    display.setTextSize(3);
    display.print("Device Ready");

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(LX, Y_BODY1);
    display.setTextSize(2);
    display.print("No content assigned yet.");

    display.setCursor(LX, Y_BODY2);
    display.setTextSize(2);
    display.print("Open the Zenso panel to assign");

    display.setCursor(LX, Y_BODY3);
    display.setTextSize(2);
    display.print("a screen to this device.");

    display.setCursor(LX, Y_BODY4);
    display.setTextSize(1);
    display.print("Device ID: ");
    display.print(device_uid);

    display.setCursor(BRANDING_X, BRANDING_Y);
    display.setTextSize(1);
    display.print("zenso.ink");

  } while (display.nextPage());

  display.powerOff();
}
