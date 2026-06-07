# Zenso Firmware

ESP32-S3 firmware for a connected 7-color e-Paper display (Waveshare 7.3" ACeP).
The device provisions via WiFi captive portal, fetches screen content from the Zenso API, and renders it on the e-paper display.

## Hardware

- **Board:** Seeed Studio XIAO ESP32-S3 (on EE04 baseboard, 8MB Flash, 8MB PSRAM)
- **Display:** Waveshare 7.3" ACeP (F) — 7-color, 800×480 px
- **Connection:** 50-pin FPC cable (jumper set to **50-Pin** on EE04)
- **No external wiring required.** See `AGENTS.md` for pin mapping.

## Build & Deploy

```bash
pio run              # build
pio run -t upload    # upload to device
pio device monitor -b 115200  # serial monitor
```

**Board config:** `seeed_xiao_esp32s3` · **Upload:** 921600 baud · **Monitor:** 115200 baud

## Boot Flow

1. **LittleFS** is mounted
2. **Identity & config** loaded from flash
3. If not provisioned or KEY1 held 3s → **captive portal** starts (SSID: `Zenso-Setup`)
4. If provisioned → **WiFi connect** → **API content fetch** via `http_fetch_display()`
5. Based on `FetchResult`:
   - `OK` → renders fetched `/display.raw`
   - `NOT_MODIFIED` → renders cached `/display.raw`
   - `NO_CONTENT` → shows "Device Ready / No content assigned" status screen
   - `ERROR` → tries cached file, falls back to status screen

## Serial Commands

Press `h` in the serial monitor to show the developer menu:

| Key | Action |
|-----|--------|
| `h` | Show developer menu |
| `c` / `ESC` | Clear display to white |
| `r` | Re-render cached `/display.raw` |
| `1` | Show 'Setup Mode' provisioning screen |
| `2` | Show 'Waiting for Authorization' screen |
| `3` | Show 'Claim Expired' screen |
| `4` | Show 'No Content' status screen |

## Project Structure

```
zenso-firmware/
├── src/
│   ├── main.cpp               # Boot, fetch, rendering
│   ├── dev_menu.cpp            # Serial developer menu
│   ├── provisioning_screen.cpp # Status screens
│   ├── http_client.cpp         # API content fetch
│   ├── wifi_manager.cpp        # WiFi + captive portal
│   ├── config.cpp              # Config persistence
│   └── device_identity.cpp     # Identity persistence
├── include/                    # Headers
├── platformio.ini
├── AGENTS.md                   # Development standards
└── README.md
```

## Dependencies

- `ZinggJM/GxEPD2` — e-paper display driver
- `adafruit/Adafruit NeoPixel` — status LED
- `ArduinoJson` — API response parsing
- `QRCode` — QR generation for provisioning

## License

Open source. See individual library licenses.
