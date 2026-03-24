#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <GxEPD2_7C.h>
#include <epd7c/GxEPD2_730c_ACeP_730.h>

#define CS_PIN    10
#define DC_PIN    8
#define RST_PIN   9
#define BUSY_PIN  14
#define SCK_PIN   12
#define MOSI_PIN  11

GxEPD2_7C<GxEPD2_730c_ACeP_730, 20> display(
  GxEPD2_730c_ACeP_730(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN)
);

uint16_t read16(File &f)
{
  uint8_t b0 = f.read();
  uint8_t b1 = f.read();
  return (uint16_t)b0 | ((uint16_t)b1 << 8);
}

uint32_t read32(File &f)
{
  uint8_t b0 = f.read();
  uint8_t b1 = f.read();
  uint8_t b2 = f.read();
  uint8_t b3 = f.read();
  return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
}

int32_t readS32(File &f)
{
  return (int32_t)read32(f);
}

struct RGB
{
  uint8_t r, g, b;
};

uint16_t mapRgbToEinkColor(uint8_t r, uint8_t g, uint8_t b)
{
  struct RefColor
  {
    uint8_t r, g, b;
    uint16_t epd;
    const char* name;
  };

  static const RefColor refs[] = {
    {255, 255, 255, GxEPD_WHITE,  "WHITE"},
    {  0,   0,   0, GxEPD_BLACK,  "BLACK"},
    {  0, 255,   0, GxEPD_GREEN,  "GREEN"},
    {  0,   0, 255, GxEPD_BLUE,   "BLUE"},
    {255,   0,   0, GxEPD_RED,    "RED"},
    {255, 255,   0, GxEPD_YELLOW, "YELLOW"},
    {255, 128,   0, GxEPD_ORANGE, "ORANGE"}
  };

  uint32_t bestDist = 0xFFFFFFFF;
  uint16_t bestColor = GxEPD_WHITE;

  for (auto &c : refs)
  {
    int32_t dr = (int32_t)r - c.r;
    int32_t dg = (int32_t)g - c.g;
    int32_t db = (int32_t)b - c.b;
    uint32_t dist = dr * dr + dg * dg + db * db;

    if (dist < bestDist)
    {
      bestDist = dist;
      bestColor = c.epd;
    }
  }

  return bestColor;
}

bool drawBMP(const char *filename)
{
  File bmpFile = LittleFS.open(filename, "r");
  if (!bmpFile)
  {
    Serial.println("BŁĄD: Nie można otworzyć pliku BMP");
    return false;
  }

  if (read16(bmpFile) != 0x4D42)
  {
    Serial.println("BŁĄD: To nie jest BMP");
    bmpFile.close();
    return false;
  }

  uint32_t fileSize   = read32(bmpFile);
  (void)fileSize;
  read32(bmpFile);
  uint32_t dataOffset = read32(bmpFile);

  uint32_t dibSize    = read32(bmpFile);
  int32_t width       = readS32(bmpFile);
  int32_t height      = readS32(bmpFile);
  uint16_t planes     = read16(bmpFile);
  uint16_t bitCount   = read16(bmpFile);
  uint32_t compression= read32(bmpFile);

  if (planes != 1)
  {
    Serial.println("BŁĄD: Niepoprawne BMP planes");
    bmpFile.close();
    return false;
  }

  if (bitCount != 4 || compression != 0)
  {
    Serial.println("BŁĄD: Obsługiwany tylko 4bpp BMP bez kompresji");
    bmpFile.close();
    return false;
  }

  bool topDown = false;
  int32_t bmpHeight = height;
  if (height < 0)
  {
    topDown = true;
    bmpHeight = -height;
  }

  if (width != display.width() || bmpHeight != display.height())
  {
    Serial.printf("UWAGA: BMP ma %ldx%ld, ekran ma %d x %d\n",
                  (long)width, (long)bmpHeight, display.width(), display.height());
  }

  uint32_t colorsInPalette = 16;
  if (dibSize >= 40)
  {
    bmpFile.seek(46);
    uint32_t clrUsed = read32(bmpFile);
    if (clrUsed > 0 && clrUsed <= 16) colorsInPalette = clrUsed;
  }

  bmpFile.seek(14 + dibSize);

  RGB palette[16];
  uint16_t epdPalette[16];

  for (uint32_t i = 0; i < 16; i++)
  {
    palette[i] = {0, 0, 0};
    epdPalette[i] = GxEPD_WHITE;
  }

  for (uint32_t i = 0; i < colorsInPalette; i++)
  {
    uint8_t b = bmpFile.read();
    uint8_t g = bmpFile.read();
    uint8_t r = bmpFile.read();
    bmpFile.read();

    palette[i] = {r, g, b};
    epdPalette[i] = mapRgbToEinkColor(r, g, b);

    Serial.printf("Pal[%lu] RGB=(%u,%u,%u)\n", (unsigned long)i, r, g, b);
  }

  uint32_t rowSize = ((width * bitCount + 31) / 32) * 4;
  uint8_t rowBuffer[rowSize];

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    for (int32_t y = 0; y < bmpHeight; y++)
    {
      uint32_t rowIndex = topDown ? y : (bmpHeight - 1 - y);
      uint32_t pos = dataOffset + rowIndex * rowSize;

      if (!bmpFile.seek(pos))
      {
        Serial.printf("BŁĄD seek row %ld\n", (long)y);
        bmpFile.close();
        return false;
      }

      size_t n = bmpFile.read(rowBuffer, rowSize);
      if (n != rowSize)
      {
        Serial.printf("BŁĄD read row %ld\n", (long)y);
        bmpFile.close();
        return false;
      }

      int32_t x = 0;
      for (uint32_t i = 0; i < rowSize && x < width; i++)
      {
        uint8_t v = rowBuffer[i];
        uint8_t hi = (v >> 4) & 0x0F;
        uint8_t lo = v & 0x0F;

        display.drawPixel(x, y, epdPalette[hi]);
        x++;

        if (x < width)
        {
          display.drawPixel(x, y, epdPalette[lo]);
          x++;
        }
      }
    }
  }
  while (display.nextPage());

  bmpFile.close();
  Serial.println("BMP wyrenderowany poprawnie");
  return true;
}

void clearToWhite()
{
  display.init(115200, true, 50, false);
  display.setFullWindow();

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  }
  while (display.nextPage());

  display.powerOff();
  Serial.println("Ekran wyczyszczony");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println("\n===== START =====");

  if (!LittleFS.begin(true))
  {
    Serial.println("BŁĄD: LittleFS");
    return;
  }

  SPI.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);

  display.init(115200, true, 50, false);
  display.setRotation(0);
  display.setFullWindow();

  if (!drawBMP("/display.bmp"))
  {
    Serial.println("Render BMP nieudany");
  }

  display.powerOff();
  Serial.println("===== GOTOWE =====");
}

void loop()
{
  if (Serial.available())
  {
    int c = Serial.read();

    if (c == 27)
    {
      clearToWhite();
    }
    else if (c == 'r' || c == 'R')
    {
      display.init(115200, true, 50, false);
      drawBMP("/display.bmp");
      display.powerOff();
    }
  }

  delay(10);
}