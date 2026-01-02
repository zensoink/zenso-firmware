#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Check pin: 48, 38, or 47 (for DevKitC-1 usually 48)
#define PIN_RGB       48
#define NUM_PIXELS    1

Adafruit_NeoPixel pixels(NUM_PIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  pixels.begin();
  pixels.setBrightness(50);
  pixels.clear();
  pixels.show();

  // Give a moment for the Serial Monitor to start
  delay(2000);

  Serial.println("==================================");
  Serial.println("   WELCOME TO THE COMMAND CENTER!   ");
  Serial.println("==================================");
  Serial.println("Type a command and press ENTER:");
  Serial.println(" [R] - Red");
  Serial.println(" [G] - Green");
  Serial.println(" [B] - Blue");
  Serial.println(" [W] - White");
  Serial.println(" [X] - Off");
  Serial.println("==================================");
}

void loop() {
  // Check if data has arrived from the computer
  if (Serial.available() > 0) {
    // Read the character
    char command = Serial.read();

    // Ignore newline characters (Enter)
    if (command == '\n' || command == '\r') return;

    Serial.print("Received command: ");
    Serial.println(command);

    // Execute action based on the character
    switch (toupper(command)) { // toupper converts 'r' to 'R'
      case 'R':
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
        Serial.println("-> LED: RED");
        break;
      case 'G':
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));
        Serial.println("-> LED: GREEN");
        break;
      case 'B':
        pixels.setPixelColor(0, pixels.Color(0, 0, 255));
        Serial.println("-> LED: BLUE");
        break;
      case 'W':
        pixels.setPixelColor(0, pixels.Color(255, 255, 255));
        Serial.println("-> LED: WHITE");
        break;
      case 'X':
        pixels.clear();
        Serial.println("-> LED: OFF");
        break;
      default:
        Serial.println("-> Unknown command! Use R, G, B, W, X.");
        // Blink red as an error indicator
        pixels.setPixelColor(0, pixels.Color(50, 0, 0));
        pixels.show();
        delay(200);
        pixels.clear();
    }
    pixels.show(); // Update the LED
  }
}
