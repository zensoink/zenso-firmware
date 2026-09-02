#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <esp_sleep.h>
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
static String device_token;
static bool showing_content = false;
static unsigned long last_check_in_ms = 0;
static unsigned long refresh_rate_ms = 60000;

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

// ponytail: display helpers wrap init+draw+powerOff in one call
static void display_init_and_draw_raw() {
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();
  if (drawRAW("/display.raw")) {
    showing_content = true;
  } else {
    provisioning_screen_draw_no_content(cfg.hardware_id);
    showing_content = false;
  }
  if (showing_content) display.powerOff();
}

static void display_init_and_draw_no_content() {
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  display.init(115200, true, 2, false);
  display.setRotation(0);
  display.setFullWindow();
  provisioning_screen_draw_no_content(cfg.hardware_id);
  showing_content = false;
}

static void enter_deep_sleep() {
  display.powerOff();
  esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_INTERVAL_MS * 1000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY1_PIN, 0); // GPIO2 wakes on LOW
  Serial.printf("Deep sleep %us (KEY1 wakes)\n", DEEP_SLEEP_INTERVAL_MS / 1000);
  esp_deep_sleep_start();
}

static void initial_fetch_and_display() {
  if (!device_login(cfg, device_token)) {
    display_init_and_draw_no_content();
    return;
  }

  DeviceStatus status;
  FetchResult ci = http_check_in(cfg, device_token, status);
  if (ci == FetchResult::OK) {
    refresh_rate_ms = (unsigned long)status.refresh_rate * 1000UL;
  }

  if (ci == FetchResult::OK && status.has_image && !status.content_changed) {
    display_init_and_draw_raw();
    return;
  }

  if (ci == FetchResult::OK && status.has_image && status.content_changed) {
    FetchResult fr = http_fetch_display_with_token(cfg, device_token);
    if (fr == FetchResult::OK || fr == FetchResult::NOT_MODIFIED) {
      display_init_and_draw_raw();
    } else {
      display_init_and_draw_no_content();
    }
    return;
  }

  if (ci != FetchResult::OK) {
    FetchResult fr = http_fetch_display_with_token(cfg, device_token);
    if (fr == FetchResult::OK || fr == FetchResult::NOT_MODIFIED) {
      display_init_and_draw_raw();
    } else {
      display_init_and_draw_no_content();
    }
    return;
  }

  display_init_and_draw_no_content();
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

  // Verify WiFi is still up before first fetch
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Setup: WiFi lost after connect, reconnecting...");
    if (!wifi_connect(cfg)) {
      Serial.println("Setup: WiFi reconnect failed — starting provisioning mode");
      wifi_start_provisioning(cfg);
      DeviceIdentity post_identity = identity_load();
      if (post_identity.hardware_id.length() > 0 && post_identity.device_secret.length() > 0) {
        cfg.hardware_id = post_identity.hardware_id;
        cfg.device_secret = post_identity.device_secret;
      }
    }
  }

  // First fetch: login + check-in + display
  initial_fetch_and_display();

  // Provisioning runs a blocking loop, so reaching here means we are claimed.
  if (cfg.hardware_id.length() > 0 && cfg.device_secret.length() > 0) {
    enter_deep_sleep();
  }

  Serial.println("Setup: not provisioned — staying awake");
}

void loop()
{
  // KEY1 pressed — clear screen
  if (digitalRead(KEY1_PIN) == LOW)
  {
    delay(50);
    while (digitalRead(KEY1_PIN) == LOW)
      delay(10);
    clearToWhite();
  }

  // Serial developer menu
  if (Serial.available() > 0)
  {
    handle_dev_command(Serial.read(), cfg);
  }

  // Only run periodic cycle when interval elapsed
  if (millis() - last_check_in_ms < refresh_rate_ms)
  {
    delay(10);
    return;
  }

  last_check_in_ms = millis();

  // Reconnect WiFi if lost
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: connection lost, reconnecting...");
    if (!wifi_connect(cfg)) {
      Serial.println("WiFi: reconnect failed, will retry next cycle");
      return;
    }
  }

  // Refresh token if missing
  if (device_token.length() == 0 && !device_login(cfg, device_token)) {
    return;
  }

  DeviceStatus status;
  FetchResult cr = http_check_in(cfg, device_token, status);

  if (cr == FetchResult::ERROR) {
    device_token = "";
    return;
  }

  refresh_rate_ms = (unsigned long)status.refresh_rate * 1000UL;

  if (status.has_image && status.content_changed) {
    FetchResult fr = http_fetch_display_with_token(cfg, device_token);
    if (fr == FetchResult::OK) {
      display_init_and_draw_raw();
    } else if (fr == FetchResult::ERROR) {
      device_token = "";
    }
  } else if (!status.has_image && showing_content) {
    display_init_and_draw_no_content();
  }

  delay(10);
}
