#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include "config.h"
#include "device_identity.h"
#include "pins.h"
#include "version.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "provisioning_screen.h"
#include "dev_menu.h"

#ifndef MAX_DISPLAY_BUFFER_SIZE
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#endif

#ifndef MAX_HEIGHT_7C
#define MAX_HEIGHT_7C(EPD) ((EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2)) ? \
                            EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2))
#endif

GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)> display(
  GxEPD2_730c_ACeP_730(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
);

static const uint16_t epdPalette[7] = {
  GxEPD_BLACK,
  GxEPD_WHITE,
  GxEPD_GREEN,
  GxEPD_BLUE,
  GxEPD_RED,
  GxEPD_YELLOW,
  GxEPD_ORANGE
};

static DeviceConfig cfg;

bool drawRAW(const char *filename)
{
  File rawFile = LittleFS.open(filename, "r");
  if (!rawFile)
  {
    Serial.println("ERROR: Cannot open RAW file");
    return false;
  }

  const int16_t w = display.width();
  const int16_t h = display.height();
  const uint32_t rowSize = (w + 1) / 2;
  const uint32_t expectedSize = rowSize * h;

  size_t fileSize = rawFile.size();
  Serial.printf("RAW: file=%s, size=%u, expected=%lu, screen=%dx%d\n",
                filename, (unsigned)fileSize, (unsigned long)expectedSize, w, h);

  if (fileSize != expectedSize)
  {
    Serial.println("ERROR: Invalid RAW file size");
    rawFile.close();
    return false;
  }

  uint8_t *rowBuffer = (uint8_t *)malloc(rowSize);
  if (!rowBuffer)
  {
    Serial.println("ERROR: No memory for rowBuffer");
    rawFile.close();
    return false;
  }

  uint32_t colorCount[16] = {0};

  display.setRotation(0);
  display.setFullWindow();

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    if (!rawFile.seek(0))
    {
      Serial.println("ERROR: seek(0) failed");
      free(rowBuffer);
      rawFile.close();
      return false;
    }

    for (int16_t y = 0; y < h; y++)
    {
      size_t n = rawFile.read(rowBuffer, rowSize);
      if (n != rowSize)
      {
        Serial.printf("ERROR: read row=%d got=%u expected=%lu\n",
                      y, (unsigned)n, (unsigned long)rowSize);
        free(rowBuffer);
        rawFile.close();
        return false;
      }

      int16_t x = 0;
      for (uint32_t i = 0; i < rowSize && x < w; i++)
      {
        uint8_t val = rowBuffer[i];
        uint8_t pixel1 = (val >> 4) & 0x0F;
        uint8_t pixel2 = val & 0x0F;

        colorCount[pixel1]++;
        display.drawPixel(x, y, (pixel1 < 7) ? epdPalette[pixel1] : GxEPD_WHITE);
        x++;

        if (x < w)
        {
          colorCount[pixel2]++;
          display.drawPixel(x, y, (pixel2 < 7) ? epdPalette[pixel2] : GxEPD_WHITE);
          x++;
        }
      }
    }
  }
  while (display.nextPage());

  for (int i = 0; i < 16; i++)
  {
    Serial.printf("IDX %d count=%lu\n", i, (unsigned long)colorCount[i]);
  }

  free(rowBuffer);
  rawFile.close();
  Serial.println("RAW rendered");
  return true;
}

void clearToWhite()
{
  Serial.println("Clearing screen...");

  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  }
  while (display.nextPage());

  display.powerOff();
  Serial.println("Screen cleared");
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  pinMode(KEY1_PIN, INPUT_PULLUP);

  Serial.println("\n===== START =====");

  if (!LittleFS.begin(true))
  {
    Serial.println("ERROR LittleFS");
    return;
  }

  Serial.println("LittleFS OK");

  config_load(cfg);

  DeviceIdentity identity = identity_load();

  if (identity.hardware_id.length() > 0 && identity.device_secret.length() > 0) {
    cfg.hardware_id = identity.hardware_id;
    cfg.device_secret = identity.device_secret;
    Serial.println("Setup: identity loaded, hardware_id=" + identity.hardware_id);
  } else {
    Serial.println("Setup: no hardware_id/secret yet — device not claimed");
  }

  // KEY1 hold detection — force provisioning if held for 3s
  bool force_provisioning = false;
  if (digitalRead(KEY1_PIN) == LOW) {
    uint32_t press_start = millis();
    force_provisioning = true;
    while (millis() - press_start < 3000) {
      delay(100);
      if (digitalRead(KEY1_PIN) != LOW) {
        force_provisioning = false;
        break;
      }
    }
  }

  if (force_provisioning) {
    Serial.println("Setup: KEY1 held — wiping identity for fresh claim");
    DeviceIdentity blank;
    blank.hardware_id = device_get_id();
    blank.bootstrap_secret = blank.hardware_id;
    identity_save(blank);
    cfg.hardware_id = "";
    cfg.device_secret = "";
    wifi_start_provisioning(cfg);
    {
      DeviceIdentity post_identity = identity_load();
      if (post_identity.hardware_id.length() > 0 && post_identity.device_secret.length() > 0) {
        cfg.hardware_id = post_identity.hardware_id;
        cfg.device_secret = post_identity.device_secret;
        Serial.println("Setup: post-provisioning identity loaded, hardware_id=" + post_identity.hardware_id);
      } else {
        Serial.println("Setup: provisioning incomplete — restarting");
        ESP.restart();
      }
    }
  } else if (!config_is_provisioned(cfg)) {
    Serial.println("Setup: no config found — starting provisioning mode");
    wifi_start_provisioning(cfg);
    {
      DeviceIdentity post_identity = identity_load();
      if (post_identity.hardware_id.length() > 0 && post_identity.device_secret.length() > 0) {
        cfg.hardware_id = post_identity.hardware_id;
        cfg.device_secret = post_identity.device_secret;
        Serial.println("Setup: post-provisioning identity loaded, hardware_id=" + post_identity.hardware_id);
      }
    }
  } else if (!wifi_connect(cfg)) {
    Serial.println("Setup: WiFi connection failed — starting provisioning mode");
    wifi_start_provisioning(cfg);
    {
      DeviceIdentity post_identity = identity_load();
      if (post_identity.hardware_id.length() > 0 && post_identity.device_secret.length() > 0) {
        cfg.hardware_id = post_identity.hardware_id;
        cfg.device_secret = post_identity.device_secret;
        Serial.println("Setup: post-provisioning identity loaded, hardware_id=" + post_identity.hardware_id);
      }
    }
  }

  Serial.println("Setup: WiFi connected, continuing...");

  FetchResult fetch_result = http_fetch_display(cfg);

  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);

  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();

  if (fetch_result == FetchResult::NO_CONTENT) {
    Serial.println("Setup: no screen assigned — drawing placeholder");
    provisioning_screen_draw_no_content(device_get_id());
  } else if (fetch_result == FetchResult::ERROR) {
    Serial.println("Setup: fetch error — trying cached /display.raw");
    if (!drawRAW("/display.raw")) {
      provisioning_screen_draw_no_content(device_get_id());
    }
  } else {
    if (!drawRAW("/display.raw")) {
      provisioning_screen_draw_no_content(device_get_id());
    }
  }

  display.powerOff();
  Serial.println("===== DONE =====");
}

void loop()
{
  if (digitalRead(KEY1_PIN) == LOW)
  {
    delay(50);
    while (digitalRead(KEY1_PIN) == LOW)
      delay(10);
    clearToWhite();
  }

  if (Serial.available() > 0)
  {
    handle_dev_command(Serial.read(), cfg);
  }

  delay(10);
}
