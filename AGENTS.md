# Zenso Firmware - Agent Guidelines & Repository Standards

## Project Overview
Zenso Display Firmware — XIAO ESP32-S3 (via Seeed EE04 baseboard) + Waveshare 7.3" ACeP e-Paper HAT (Model F) integration with GxEPD2 library.
- **Board:** Seeed Studio XIAO ESP32-S3 (on EE04 baseboard, 8MB Flash, 8MB PSRAM)
- **Display:** Waveshare 7.3" ACeP (7-color e-paper, 800×480 px, ACeP_730) via 50-pin FPC
- **Library:** GxEPD2 (maintained, open-source)
- **Communication:** 4-line SPI interface

---

## Hardware Setup

### EE04 Pin Reference (XIAO ESP32-S3 → 7.3" ACeP via 50-pin FPC)
The display connects directly to the EE04 baseboard via the 50-pin FPC connector (jumper set to **50-Pin**). No external wiring is needed.

| Display Signal | XIAO GPIO | XIAO Pin Label |
|----------------|-----------|----------------|
| MOSI (SPI) | GPIO9 | D10 |
| SCLK (SPI) | GPIO7 | D8 |
| CS | GPIO44 | D7 (RX) |
| DC | GPIO10 | — (Plus pad) |
| RST | GPIO38 | — (Plus pad) |
| BUSY | GPIO4 | D3 |

### SPI Configuration
- **Mode:** 4-line SPI
- **Speed:** 115200 baud (configurable via display.init())
- **Power:** 3.3V from XIAO (supplied by EE04 baseboard)

### Pin Definitions (src/main.cpp)
```cpp
#define CS_PIN    44
#define DC_PIN    10
#define RST_PIN   38
#define BUSY_PIN  4

#define SCK_PIN   7
#define MOSI_PIN  9
```

---

## Display Specifications
- **Model:** Waveshare 7.3" ACeP (F) — ACeP_730
- **Type:** 7-color e-paper (black, white, red, yellow, green, blue, orange)
- **Resolution:** 800 × 480 pixels
- **Refresh Mode:** Full + partial refresh
- **Power:** ~50mA active, <1mA sleep
- **Class in GxEPD2:** `GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)>` (7-color GFX wrapper with page buffer)

---

## Build & Deploy Commands

### Basic Commands
- **Build:** `pio run`
- **Upload:** `pio run -t upload`
- **Clean:** `pio run -t clean`
- **Monitor (Serial):** `pio device monitor -b 115200`
- **Rebuild All:** `pio run -t clean && pio run`

### Configuration
- **Board:** `seeed_xiao_esp32s3`
- **Platform:** `espressif32`
- **Framework:** `arduino`
- **Upload Speed:** `921600` baud
- **Monitor Speed:** `115200` baud

### PlatformIO Configuration (platformio.ini)
```ini
[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino

board_build.filesystem = littlefs

monitor_speed = 115200
upload_speed = 921600

build_flags = 
    -DCORE_DEBUG_LEVEL=1

lib_deps =
    adafruit/Adafruit NeoPixel @ ^1.12.0
    ZinggJM/GxEPD2 @ ^1.6.8
```

---

## Code Style & Standards

### C++ & Arduino Framework
- **Language:** C++17 (Arduino framework)
- **Indentation:** 2 spaces (consistent)
- **Line Breaks:** Unix LF, no Windows CRLF

### Naming Conventions
- **Variables & Functions:** `snake_case` (lowercase with underscores)
- **Constants:** `UPPER_CASE` (all caps with underscores)
- **Classes:** `PascalCase` (if custom classes needed)
- **Example:**
  ```cpp
  #define DISPLAY_WIDTH 800
  uint8_t refresh_mode = 0;
  void initialize_display() { /* ... */ }
  ```

### Includes Order (Mandatory)
1. System headers (`<Arduino.h>`, `<SPI.h>`)
2. Library headers (`<GxEPD2_7C.h>`, `<Fonts/...>`)
3. Local headers (`#include "local.h"`)

```cpp
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
#include <Fonts/FreeMono9pt7b.h>
// (no local headers in current project)
```

### Type Usage
- **Prefer:** `uint8_t`, `uint16_t`, `uint32_t` (explicit width)
- **Avoid:** raw `int`, `unsigned int` (platform-dependent)
- **GPIO pins:** `uint8_t` or `#define` constants
- **Timings:** `uint32_t` (milliseconds)

### Comments & Documentation
- **Language:** English (descriptive, concise)
- **Style:** `//` for inline, `/* */` for block comments
- **Required:** All non-obvious logic
- **Example:**
  ```cpp
  // Clear display to white background
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
  ```

### Error Handling & Validation
- Always validate pin configurations at startup
- Log initialization status via Serial.println()
- Use descriptive error messages (e.g., "Display init failed")
- Check for timeout conditions in loops
- Return meaningful status codes (0 = success)

### Serial Debugging
- **Speed:** 115200 baud (fixed in platformio.ini)
- **Format:** Use `Serial.println()` for status messages
- **Markers:** Use visual separators (e.g., `════════════════════════`)
- **Example:**
  ```cpp
  Serial.println("Initializing display...");
  display.init(115200);
  Serial.println("✓ Display initialized");
  ```

---

## Project Structure

```
zenso-firmware/
├── src/
│   └── main.cpp              // Main firmware code
├── lib/
│   ├── .gitkeep             // Empty (dependencies via PlatformIO)
├── include/
│   └── .gitkeep             // For future local headers
├── platformio.ini           // PlatformIO configuration
├── AGENTS.md                // This file (AI agent guidelines)
├── README.md                // User-facing documentation
└── .vscode/
    └── c_cpp_properties.json // IntelliSense configuration
```

### Key Files
- **src/main.cpp** — Display initialization, test rendering, sleep mode
- **platformio.ini** — Build settings, library dependencies, compiler flags
- **.vscode/c_cpp_properties.json** — IntelliSense include paths for VS Code

---

## GxEPD2 Library Integration

### Display Initialization
```cpp
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>
GxEPD2_7C<GxEPD2_730c_ACeP_730, MAX_HEIGHT_7C(GxEPD2_730c_ACeP_730)> display(
  GxEPD2_730c_ACeP_730(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

void setup() {
  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
  display.init(115200);
}
```

### Common Operations
- **Clear to white:** `display.fillScreen(GxEPD_WHITE);`
- **Set text color:** `display.setTextColor(GxEPD_BLACK);`
- **Draw text:** Use `display.println()` with fonts
- **Partial refresh:** `display.setPartialWindow()` (advanced)
- **Sleep mode:** `display.powerOff();`
- **Wake up:** `display.init()` again

### Page-Based Rendering
```cpp
display.firstPage();
do {
  display.fillScreen(GxEPD_WHITE);
  display.setCursor(50, 50);
  display.println("Hello");
} while (display.nextPage());
```

---

**Last Updated:** 2026-05-22
**Maintainer:** Konrad Stępiń (@konradstepien)
