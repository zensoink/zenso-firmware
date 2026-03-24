// File: src/main.cpp
// ESP32-S3 + Waveshare 7.3" e-Paper HAT (F) (ACeP 7-color)
// GxEPD2 + Serial ESC handler to clear-to-white and power off
// Notes:
// - Page buffer height is compile-time. We use MAX_HEIGHT_7C(...) like in GxEPD2 examples.

#include <Arduino.h>
#include <SPI.h>

#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include <Fonts/FreeMono9pt7b.h>

// ---- Pins (your wiring) ----
#define CS_PIN    10
#define DC_PIN    8
#define RST_PIN   9
#define BUSY_PIN  14

#define SCK_PIN   12
#define MOSI_PIN  11

// ---- GxEPD2 "generic" page height selection ----
// This macro is used in the official GxEPD2 display selection headers. [web:832]
#ifndef MAX_DISPLAY_BUFFER_SIZE
// A safe default if not provided by the build; keeps memory under control.
// You can tune this value if you want faster refresh vs RAM usage.
#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#endif

#ifndef MAX_HEIGHT_7C
// For 7-color (4bpp), GxEPD2 selection uses WIDTH/2 bytes per line (nibbles). [web:832]
#define MAX_HEIGHT_7C(EPD) ( (EPD::HEIGHT <= (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2)) ? \
                             EPD::HEIGHT : (MAX_DISPLAY_BUFFER_SIZE) / (EPD::WIDTH / 2) )
#endif

// ---- Display object ----
GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)> display(
  GxEPD2_730c_ACeP_730(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
);

// Helper: draw initial test pattern (color bars)
static void drawColorBars()
{
  display.setRotation(0);
  display.setFont(&FreeMono9pt7b);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.fillRect(20,  40, 760, 60, GxEPD_BLACK);
    display.fillRect(20, 120, 760, 60, GxEPD_RED);
    display.fillRect(20, 200, 760, 60, GxEPD_BLUE);
    display.fillRect(20, 280, 760, 60, GxEPD_GREEN);
    display.fillRect(20, 360, 760, 60, GxEPD_YELLOW);

    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, 470);
    display.print("Press ESC to clear & power off");
  } while (display.nextPage());
}

// Helper: clear to white + power off (triggered by ESC)
static void clearAndPowerOff()
{
  Serial.println("ESC detected -> clear to white + power off");

  // Ensure the controller is active (in case it was powered off earlier)
  display.init(115200);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());

  // ACeP is slow; give it enough time to visibly complete the refresh.
  Serial.println("Waiting 40s for full white refresh...");
  delay(40000);

  display.powerOff();
  Serial.println("Display powered off.");
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("===== ZENSO 7.3\" ACeP (F) TEST =====");
  Serial.printf("Pins: CS=%d DC=%d RST=%d BUSY=%d SCK=%d MOSI=%d\n",
                CS_PIN, DC_PIN, RST_PIN, BUSY_PIN, SCK_PIN, MOSI_PIN);
  Serial.println("Press ESC (ASCII 27) in the serial terminal to clear & power off.");

  pinMode(BUSY_PIN, INPUT);

  // Force SPI pins to match your wiring
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);

  // Init display
  display.init(115200);

  // Draw something right away
  drawColorBars();

  Serial.println("Initial pattern drawn. Waiting for user input...");
}

void loop()
{
  // Read single characters from Serial
  if (Serial.available() > 0) {
    int c = Serial.read();

    // ASCII 27 is ESC
    if (c == 27) {
      clearAndPowerOff();
    }
  }

  delay(20);
}
