#include <Arduino.h>
#include <SPI.h>
#include <qrcode.h>
#include "display_manager.h"
#include "pins.h"
#include "provisioning_screen.h"

void provisioning_screen_init() {
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  disp->init(115200, true, 2, false);
  disp->setRotation(0);
  disp->setFullWindow();
}

void provisioning_screen_draw(
  const String &ap_ssid,
  const String &ap_password,
  const String &ap_url,
  const String &firmware_version)
{
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  const int16_t w = disp->width();
  const int16_t h = disp->height();

  const int16_t qr_module = (w < 600) ? 3 : ((w < 800) ? 4 : 5);
  const int16_t qr_x = (w < 600) ? 20 : 35;
  const int16_t qr_y = (h < 400) ? 30 : 50;
  const int16_t right_x = (w < 600) ? 180 : ((w < 800) ? 240 : 310);
  const int16_t branding_x = w - 100;
  const int16_t branding_y = h - 25;

  const uint8_t title_size = (w < 600) ? 2 : 3;
  const uint8_t text_size = (w < 600) ? 1 : 2;

  disp->firstPage();
  do {
    disp->fillScreen(GxEPD_WHITE);

    // ---- QR code (WPA credentials) ----
    String wifi_qr = "WIFI:T:WPA;S:" + ap_ssid + ";P:" + ap_password + ";;";

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)];

    if (qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, wifi_qr.c_str()) == 0) {
      uint16_t qr_size = qrcode.size * qr_module;

      disp->fillRect(qr_x - qr_module, qr_y - qr_module,
                     qr_size + 2 * qr_module, qr_size + 2 * qr_module, GxEPD_WHITE);

      for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
          if (qrcode_getModule(&qrcode, x, y)) {
            disp->fillRect(qr_x + x * qr_module, qr_y + y * qr_module,
                           qr_module, qr_module, GxEPD_BLACK);
          }
        }
      }
    }

    // ---- Text ----
    disp->setTextColor(GxEPD_BLACK);

    disp->setCursor(right_x, (h < 400) ? 40 : 80);
    disp->setTextSize(title_size);
    disp->print("Welcome in Zenso!");

    disp->setCursor(right_x, (h < 400) ? 80 : 140);
    disp->setTextSize(text_size);
    disp->print("1. Scan QR or connect to WiFi:");

    disp->setCursor(right_x, (h < 400) ? 105 : 170);
    disp->setTextSize(text_size);
    disp->print("   SSID: ");
    disp->print(ap_ssid);

    disp->setCursor(right_x, (h < 400) ? 130 : 200);
    disp->setTextSize(text_size);
    disp->print("   Password: ");
    disp->print(ap_password);

    disp->setCursor(right_x, (h < 400) ? 165 : 250);
    disp->setTextSize(text_size);
    disp->print("2. Open in browser:");

    disp->setCursor(right_x, (h < 400) ? 190 : 280);
    disp->setTextSize(text_size);
    disp->print("   ");
    disp->print(ap_url);

    // Bottom: Firmware version + branding
    String fw_str = "Firmware: " + firmware_version;
    int16_t x1, y1;
    uint16_t fw_w, fw_h;
    disp->setTextSize(1);
    disp->getTextBounds(fw_str, 0, 0, &x1, &y1, &fw_w, &fw_h);
    disp->setCursor(branding_x - fw_w - 20, branding_y);
    disp->print(fw_str);

    disp->setCursor(branding_x, branding_y);
    disp->print("zenso.ink");

  } while (disp->nextPage());

  disp->powerOff();
}

void provisioning_screen_draw_waiting(const String &claim_url, const String &wifi_ip) {
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  const int16_t w = disp->width();
  const int16_t h = disp->height();

  const int16_t qr_module = (w < 600) ? 3 : ((w < 800) ? 4 : 5);
  const int16_t qr_x = (w < 600) ? 20 : 35;
  const int16_t qr_y = (h < 400) ? 30 : 50;
  const int16_t right_x = (w < 600) ? 180 : ((w < 800) ? 240 : 310);
  const int16_t branding_x = w - 100;
  const int16_t branding_y = h - 25;

  const uint8_t title_size = (w < 600) ? 2 : 3;
  const uint8_t text_size = (w < 600) ? 1 : 2;

  disp->firstPage();
  do {
    disp->fillScreen(GxEPD_WHITE);

    // ---- QR code (claim URL) ----
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(4)];

    if (qrcode_initText(&qrcode, qrcodeData, 4, ECC_LOW, claim_url.c_str()) == 0) {
      uint16_t qr_size = qrcode.size * qr_module;

      disp->fillRect(qr_x - qr_module, qr_y - qr_module,
                     qr_size + 2 * qr_module, qr_size + 2 * qr_module, GxEPD_WHITE);

      for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
          if (qrcode_getModule(&qrcode, x, y)) {
            disp->fillRect(qr_x + x * qr_module, qr_y + y * qr_module,
                           qr_module, qr_module, GxEPD_BLACK);
          }
        }
      }
    }

    // ---- Text ----
    disp->setTextColor(GxEPD_BLACK);

    disp->setCursor(right_x, (h < 400) ? 35 : 60);
    disp->setTextSize(title_size);
    disp->print("Waiting for");

    disp->setCursor(right_x, (h < 400) ? 65 : 100);
    disp->setTextSize(title_size);
    disp->print("Authorization");

    disp->setCursor(right_x, (h < 400) ? 100 : 160);
    disp->setTextSize(text_size);
    disp->print("WiFi connected: ");
    disp->print(wifi_ip);

    disp->setCursor(right_x, (h < 400) ? 130 : 200);
    disp->setTextSize(text_size);
    disp->print("Scan QR or open link in browser");

    disp->setCursor(right_x, (h < 400) ? 155 : 240);
    disp->setTextSize(text_size);
    disp->print("to claim this device");

    disp->setCursor(right_x, (h < 400) ? 190 : 290);
    disp->setTextSize(1);
    disp->print(claim_url);

    disp->setCursor(branding_x, branding_y);
    disp->setTextSize(1);
    disp->print("zenso.ink");

  } while (disp->nextPage());

  disp->powerOff();
}

void provisioning_screen_draw_claim_expired() {
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  const int16_t w = disp->width();
  const int16_t h = disp->height();

  const int16_t lx = (w < 600) ? 25 : 55;
  const int16_t y_title = (h < 400) ? 40 : 100;
  const int16_t y_body1 = (h < 400) ? 80 : 160;
  const int16_t y_body2 = (h < 400) ? 115 : 200;
  const int16_t y_body3 = (h < 400) ? 150 : 240;
  const int16_t y_body4 = (h < 400) ? 185 : 290;
  const int16_t branding_x = w - 100;
  const int16_t branding_y = h - 25;

  const uint8_t title_size = (w < 600) ? 2 : 3;
  const uint8_t text_size = (w < 600) ? 1 : 2;

  disp->firstPage();
  do {
    disp->fillScreen(GxEPD_WHITE);

    disp->setTextColor(GxEPD_BLACK);
    disp->setCursor(lx, y_title);
    disp->setTextSize(title_size);
    disp->print("Claim Expired");

    disp->setCursor(lx, y_body1);
    disp->setTextSize(text_size);
    disp->print("Authorization window has closed.");

    disp->setCursor(lx, y_body2);
    disp->setTextSize(text_size);
    disp->print("To try again:");

    disp->setCursor(lx, y_body3);
    disp->setTextSize(text_size);
    disp->print("Hold KEY1 for 3 seconds");

    disp->setCursor(lx, y_body4);
    disp->setTextSize(text_size);
    disp->print("to restart provisioning.");

    disp->setCursor(branding_x, branding_y);
    disp->setTextSize(1);
    disp->print("zenso.ink");

  } while (disp->nextPage());

  disp->powerOff();
}

void provisioning_screen_draw_no_content(const String &hardware_id) {
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  const int16_t w = disp->width();
  const int16_t h = disp->height();

  const int16_t lx = (w < 600) ? 25 : 55;
  const int16_t y_title = (h < 400) ? 40 : 100;
  const int16_t y_body1 = (h < 400) ? 80 : 160;
  const int16_t y_body2 = (h < 400) ? 115 : 200;
  const int16_t y_body3 = (h < 400) ? 150 : 240;
  const int16_t y_body4 = (h < 400) ? 185 : 290;
  const int16_t branding_x = w - 100;
  const int16_t branding_y = h - 25;

  const uint8_t title_size = (w < 600) ? 2 : 3;
  const uint8_t text_size = (w < 600) ? 1 : 2;

  disp->firstPage();
  do {
    disp->fillScreen(GxEPD_WHITE);

    disp->setTextColor(GxEPD_BLACK);
    disp->setCursor(lx, y_title);
    disp->setTextSize(title_size);
    disp->print("Device Ready");

    disp->setCursor(lx, y_body1);
    disp->setTextSize(text_size);
    disp->print("No content assigned yet.");

    disp->setCursor(lx, y_body2);
    disp->setTextSize(text_size);
    disp->print("Open the Zenso panel to assign");

    disp->setCursor(lx, y_body3);
    disp->setTextSize(text_size);
    disp->print("a screen to this device.");

    disp->setCursor(lx, y_body4);
    disp->setTextSize(1);
    disp->print("Device ID: ");
    disp->print(hardware_id);

    disp->setCursor(branding_x, branding_y);
    disp->setTextSize(1);
    disp->print("zenso.ink");

  } while (disp->nextPage());

  disp->powerOff();
}
