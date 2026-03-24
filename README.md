# Zenso Firmware

ESP32-S3 firmware for controlling a Waveshare 7.3" ACeP (7-color) e-Paper display. This project demonstrates basic display initialization, color bar rendering, and power management.

## Overview

The Zenso firmware provides a foundation for e-paper display projects using the ESP32-S3 microcontroller and GxEPD2 library. It initializes the display, draws a test pattern with colored bars, and includes functionality to clear the screen and power off the display via serial command.

## Hardware Requirements

- **ESP32-S3-DevKitC-1** (8MB Flash, 320KB RAM)
- **Waveshare 7.3" ACeP e-Paper HAT (Model F)** (7-color, 800×480 px)
- **Dupont jumper wires** (M-M cables for wiring)

## Wiring Setup

Connect the HAT to the ESP32-S3 using the following pin mapping:

| HAT Pin | ESP32-S3 GPIO | Function |
|---------|---------------|----------|
| VCC | 3V3 | Power 3.3V |
| GND | GND | Ground (×2 minimum) |
| DIN | GPIO 11 | MOSI (SPI) |
| SCLK | GPIO 12 | Clock (SPI) |
| CS | GPIO 10 | Chip Select |
| DC | GPIO 8 | Data/Command |
| RST | GPIO 9 | Reset |
| BUSY | GPIO 14 | Busy Status |

**Note:** Ensure the HAT switch is set to position 0 (4-line SPI).

## Software Setup

### Prerequisites

1. Install [PlatformIO](https://platformio.org/) IDE or VS Code extension
2. Clone or download this repository

### Building the Project

```bash
# Navigate to the project directory
cd zenso-firmware

# Build the firmware
pio run

# Upload to ESP32-S3
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

### Configuration

The project uses the following PlatformIO configuration:
- **Board:** `esp32-s3-devkitc-1`
- **Framework:** Arduino
- **Upload Speed:** 921600 baud
- **Monitor Speed:** 115200 baud

## Usage

1. **Power on the device** - The display will automatically initialize and show a color bar test pattern
2. **View the output** - Color bars in black, red, blue, green, and yellow will be displayed
3. **Clear and power off** - Send ESC (ASCII 27) via the serial terminal to clear the display to white and power it off

### Serial Commands

- **ESC key** (ASCII 27): Clears the display to white and powers it off

## Project Structure

```
zenso-firmware/
├── src/
│   └── main.cpp              # Main firmware code
├── include/
│   └── .gitkeep             # Placeholder for future headers
├── lib/
│   └── .gitkeep             # Dependencies managed by PlatformIO
├── platformio.ini           # PlatformIO configuration
├── AGENTS.md                # Agent guidelines (internal)
├── README.md                # This file
└── .vscode/
    └── c_cpp_properties.json # VS Code configuration
```

## Technical Details

- **Display Resolution:** 800 × 480 pixels
- **Colors:** 7-color e-paper (black, white, red, yellow, green, blue, orange)
- **SPI Mode:** 4-line SPI at 115200 baud
- **Memory Management:** Uses page-based rendering with buffer height optimization
- **Power Consumption:** ~50mA active, <1mA in sleep mode

## Troubleshooting

### Display Not Initializing
- Verify all wiring connections match the pin table
- Ensure HAT switch is set to position 0
- Check serial output for error messages

### Colors Not Displaying Correctly
- Confirm you're using the ACeP (F) model HAT
- Verify GxEPD2 library version (^1.6.5)

### Serial Communication Issues
- Use correct monitor speed: 115200 baud
- Ensure no other programs are using the serial port

## Development

This firmware serves as a starting point for more complex e-paper applications. Key areas for extension:
- Custom graphics rendering
- Text display with different fonts
- Partial refresh for dynamic content
- Sensor integration
- Wireless connectivity

## License

This project is open-source. See individual library licenses for details.

## Contributing

Contributions are welcome! Please ensure code follows the standards outlined in AGENTS.md.