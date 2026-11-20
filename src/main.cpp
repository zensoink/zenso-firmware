#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <LittleFS.h>
#include "display_manager.h"
#include "config.h"
#include "device_identity.h"
#include "pins.h"
#include "version.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "provisioning_screen.h"
#include "dev_menu.h"

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

RTC_DATA_ATTR static bool rtc_display_in_deep_sleep = false;
RTC_DATA_ATTR static char rtc_cached_etag[64] = {0};
RTC_DATA_ATTR static uint8_t rtc_offline_retry_count = 0;

static void enter_deep_sleep(uint32_t sleep_ms = 0, bool bypass_display_power_off = false) {
  if (sleep_ms < 10000UL) {
    sleep_ms = (cfg.refresh_rate > 0 ? (uint32_t)cfg.refresh_rate : DEFAULT_REFRESH_RATE_S) * 1000UL;
  }
  if (!bypass_display_power_off) {
    DisplayManager::instance().power_off();
  }
  esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)KEY1_PIN, 0); // GPIO2 wakes on LOW
  Serial.printf("[Power] Deep sleep %" PRIu32 " ms (KEY1 wakes)\n", sleep_ms);
  esp_deep_sleep_start();
}

static void handle_offline_backoff(bool silent = false) {
  uint32_t backoff_s = 300;
  switch (rtc_offline_retry_count) {
    case 0: backoff_s = 300; break;
    case 1: backoff_s = 900; break;
    case 2: backoff_s = 1800; break;
    default: backoff_s = 3600; break;
  }
  if (rtc_offline_retry_count < 255) {
    rtc_offline_retry_count++;
  }
  Serial.printf("[Power] Offline failure (attempt %u). Backoff sleep for %" PRIu32 " s (silent=%d)...\n",
                rtc_offline_retry_count, backoff_s, silent ? 1 : 0);
  enter_deep_sleep(backoff_s * 1000UL, silent);
}

bool drawRAW(const char *filename)
{
  File rawFile = LittleFS.open(filename, "r");
  if (!rawFile)
  {
    Serial.println("ERROR: Cannot open RAW file");
    return false;
  }

  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  const int16_t w = disp->width();
  const int16_t h = disp->height();
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

  disp->setRotation(0);
  disp->setFullWindow();

  Serial.printf("RAW: Rendering pixels to e-paper panel (%dx%d)... refresh in progress\n", w, h);
  disp->firstPage();
  do
  {
    disp->fillScreen(GxEPD_WHITE);

    if (!rawFile.seek(0))
    {
      Serial.println("ERROR: seek(0) failed");
      free(rowBuffer);
      rawFile.close();
      return false;
    }

    for (int16_t y = 0; y < h; y++)
    {
      if ((y % 20) == 0)
      {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
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
        disp->drawPixel(x, y, (pixel1 < 7) ? epdPalette[pixel1] : GxEPD_WHITE);
        x++;

        if (x < w)
        {
          colorCount[pixel2]++;
          disp->drawPixel(x, y, (pixel2 < 7) ? epdPalette[pixel2] : GxEPD_WHITE);
          x++;
        }
      }
    }
  }
  while (disp->nextPage());

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
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  disp->init(115200, true, 2, false);
  disp->setRotation(0);
  disp->setFullWindow();

  disp->firstPage();
  do
  {
    disp->fillScreen(GxEPD_WHITE);
  }
  while (disp->nextPage());

  disp->powerOff();
  showing_content = false;
  Serial.println("Screen cleared");
}

static void display_init_and_draw_raw() {
  Serial.println("[Display] Initializing e-paper display hardware...");
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  disp->init(115200, true, 2, false);
  disp->setRotation(0);
  disp->setFullWindow();
  Serial.println("[Display] Reading and rendering /display.raw...");
  if (drawRAW("/display.raw")) {
    showing_content = true;
    Serial.println("[Display] RAW render successful.");
  } else {
    Serial.println("[Display] RAW render failed, showing fallback screen.");
    provisioning_screen_draw_no_content(device_get_id());
    showing_content = false;
  }
  if (showing_content) {
    Serial.println("[Display] Powering off panel controller to preserve e-ink.");
    disp->powerOff();
  }
}

static void display_init_and_draw_no_content() {
  Serial.println("[Display] Showing 'No Content' placeholder screen...");
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  GxEPD2_GFX* disp = DisplayManager::instance().get_display();
  disp->init(115200, true, 2, false);
  disp->setRotation(0);
  disp->setFullWindow();
  provisioning_screen_draw_no_content(device_get_id());
  showing_content = false;
  disp->powerOff();
}


void setup()
{
  Serial.begin(115200);
  delay(2000);

  // Disable hardware watchdogs to prevent reset during long e-paper refresh cycles (~15s)
  disableCore0WDT();
#ifndef CONFIG_FREERTOS_UNICORE
  disableCore1WDT();
#endif
  disableLoopWDT();

  pinMode(KEY1_PIN, INPUT_PULLUP);

  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  WakeupReason wakeup_reason = WakeupReason::BOOT;
  if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
    wakeup_reason = WakeupReason::TIMER;
    Serial.println("[Power] Woken by timer");
  } else if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    wakeup_reason = WakeupReason::MANUAL;
    Serial.println("[Power] Woken by KEY1 button press — resetting offline retry counter");
    rtc_offline_retry_count = 0;
  } else {
    Serial.println("[Power] Cold boot / reset — initializing RTC variables");
    rtc_display_in_deep_sleep = false;
    rtc_cached_etag[0] = '\0';
    rtc_offline_retry_count = 0;
  }

  Serial.println("\n===== START =====");

  if (!LittleFS.begin(true))
  {
    Serial.println("ERROR LittleFS");
    return;
  }

  Serial.println("LittleFS OK");

  config_load(cfg);
  if (cfg.refresh_rate <= 0) cfg.refresh_rate = DEFAULT_REFRESH_RATE_S;
  refresh_rate_ms = (unsigned long)cfg.refresh_rate * 1000UL;
  DisplayManager::instance().init(cfg.display_profile);

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
    if (cfg.device_secret.length() > 0) {
      Serial.println("Setup: WiFi connection failed for claimed device");
      handle_offline_backoff(rtc_display_in_deep_sleep && (wakeup_reason == WakeupReason::TIMER));
    } else {
      Serial.println("Setup: WiFi connection failed — starting provisioning mode");
      wifi_start_provisioning(cfg);
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
      if (cfg.device_secret.length() > 0) {
        handle_offline_backoff(rtc_display_in_deep_sleep && (wakeup_reason == WakeupReason::TIMER));
      } else {
        Serial.println("Setup: WiFi reconnect failed — starting provisioning mode");
        wifi_start_provisioning(cfg);
      }
    }
  }

  // Claimed device check-in and display fetch flow
  Serial.println("[Setup] Logging in to backend...");
  if (!device_login(cfg, device_token)) {
    Serial.println("[Setup] Device login failed.");
    if (cfg.device_secret.length() > 0) {
      handle_offline_backoff(rtc_display_in_deep_sleep && (wakeup_reason == WakeupReason::TIMER));
    } else {
      if (!LittleFS.exists("/display.raw") && !showing_content) {
        display_init_and_draw_no_content();
      }
      return;
    }
  }

  DeviceStatus status;
  DisplayState display_state = rtc_display_in_deep_sleep ? DisplayState::DEEP_SLEEP : DisplayState::ACTIVE;
  Serial.printf("[Setup] Checking in with backend (wakeup=%d, display=%d)...\n",
                (int)wakeup_reason, (int)display_state);
  FetchResult ci = http_check_in(cfg, device_token, status, wakeup_reason, display_state);

  if (ci == FetchResult::SERVER_ERROR) {
    handle_offline_backoff(rtc_display_in_deep_sleep && (wakeup_reason == WakeupReason::TIMER));
  } else if (ci != FetchResult::OK) {
    Serial.println("[Setup] Check-in failed with error");
    if (cfg.device_secret.length() > 0) {
      handle_offline_backoff(rtc_display_in_deep_sleep && (wakeup_reason == WakeupReason::TIMER));
    }
  } else {
    // FetchResult::OK
    if (status.refresh_rate > 0 && status.refresh_rate != cfg.refresh_rate) {
      Serial.printf("[Setup] Backend refresh rate update: %d -> %d s\n", cfg.refresh_rate, status.refresh_rate);
      cfg.refresh_rate = status.refresh_rate;
      config_save(cfg);
    }
    refresh_rate_ms = (unsigned long)(cfg.refresh_rate > 0 ? cfg.refresh_rate : status.refresh_rate) * 1000UL;
    if (status.display_profile.length() > 0 && status.display_profile != cfg.display_profile) {
      Serial.printf("[Setup] Backend profile update: %s -> %s\n", cfg.display_profile.c_str(), status.display_profile.c_str());
      cfg.display_profile = status.display_profile;
      config_save(cfg);
      DisplayManager::instance().switch_profile(cfg.display_profile);
    }

    uint32_t sleep_ms = (status.refresh_rate > 0 ? (uint32_t)status.refresh_rate : (uint32_t)cfg.refresh_rate) * 1000UL;

    // Server-dictated dumb execution
    switch (status.action) {
      case DisplayAction::RENDER: {
        Serial.println("[Display] Server directive: RENDER");
        FetchResult fr = http_fetch_display_with_token(cfg, device_token, rtc_cached_etag, sizeof(rtc_cached_etag));
        if (fr == FetchResult::OK || fr == FetchResult::NOT_MODIFIED) {
          display_init_and_draw_raw();
          rtc_display_in_deep_sleep = false;
          rtc_offline_retry_count = 0;
          enter_deep_sleep(sleep_ms, false);
        } else if (fr == FetchResult::SERVER_ERROR) {
          handle_offline_backoff(false);
        } else {
          enter_deep_sleep(sleep_ms, false);
        }
        break;
      }

      case DisplayAction::CLEAR: {
        Serial.println("[Display] Server directive: CLEAR");
        clearToWhite();
        rtc_display_in_deep_sleep = true;
        rtc_offline_retry_count = 0;
        enter_deep_sleep(sleep_ms, true);
        break;
      }

      case DisplayAction::NONE:
      default: {
        Serial.printf("[Display] Server directive: NONE (silent=%d)\n", rtc_display_in_deep_sleep ? 1 : 0);
        rtc_offline_retry_count = 0;
        enter_deep_sleep(sleep_ms, rtc_display_in_deep_sleep);
        break;
      }
    }
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
  DisplayState display_state = rtc_display_in_deep_sleep ? DisplayState::DEEP_SLEEP : DisplayState::ACTIVE;
  FetchResult cr = http_check_in(cfg, device_token, status, WakeupReason::BOOT, display_state);

  if (cr == FetchResult::ERROR || cr == FetchResult::SERVER_ERROR) {
    device_token = "";
    return;
  }

  if (status.refresh_rate > 0 && status.refresh_rate != cfg.refresh_rate) {
    Serial.printf("Backend refresh rate update: %d -> %d s\n", cfg.refresh_rate, status.refresh_rate);
    cfg.refresh_rate = status.refresh_rate;
    config_save(cfg);
  }
  refresh_rate_ms = (unsigned long)(cfg.refresh_rate > 0 ? cfg.refresh_rate : status.refresh_rate) * 1000UL;
  if (status.display_profile.length() > 0 && status.display_profile != cfg.display_profile) {
    Serial.printf("Backend profile update: %s -> %s\n", cfg.display_profile.c_str(), status.display_profile.c_str());
    cfg.display_profile = status.display_profile;
    config_save(cfg);
    DisplayManager::instance().switch_profile(cfg.display_profile);
  }

  switch (status.action) {
    case DisplayAction::RENDER: {
      FetchResult fr = http_fetch_display_with_token(cfg, device_token, rtc_cached_etag, sizeof(rtc_cached_etag));
      if (fr == FetchResult::OK || fr == FetchResult::NOT_MODIFIED) {
        display_init_and_draw_raw();
        rtc_display_in_deep_sleep = false;
      } else if (fr == FetchResult::ERROR || fr == FetchResult::SERVER_ERROR) {
        device_token = "";
      }
      break;
    }

    case DisplayAction::CLEAR: {
      clearToWhite();
      rtc_display_in_deep_sleep = true;
      break;
    }

    case DisplayAction::NONE:
    default:
      break;
  }

  delay(10);
}
