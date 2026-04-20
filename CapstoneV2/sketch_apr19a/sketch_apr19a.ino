#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AS726x.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ----------------------
// OLED I2C
// ----------------------
#define I2C_SDA 8
#define I2C_SCL 9

// ----------------------
// DS18B20
// ----------------------
#define TEMP_PIN 4

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// ----------------------
// OLED
// ----------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C   // change to 0x3D if needed

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----------------------
// Color Sensor
// ----------------------
Adafruit_AS726x sensor;

// ----------------------

const char* detectColor(float v, float b, float g, float y, float o, float r)
{
  float vals[6] = {v, b, g, y, o, r};
  const char* names[6] = {"VIOLET", "BLUE", "GREEN", "YELLOW", "ORANGE", "RED"};

  int maxI = 0;
  for (int i = 1; i < 6; i++) {
    if (vals[i] > vals[maxI]) maxI = i;
  }

  float total = v + b + g + y + o + r;
  if (total < 5.0f) return "NO SIGNAL";

  return names[maxI];
}

void showText(const char* line1,
              const char* line2 = "",
              const char* line3 = "")
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println(line1);
  if (line2[0]) display.println(line2);
  if (line3[0]) display.println(line3);

  display.display();
}

void setup()
{
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (1) delay(10);
  }

  showText("Booting...");

  tempSensor.begin();

  if (!sensor.begin(&Wire)) {
    showText("AS726x FAIL", "Check wiring");
    while (1) delay(10);
  }

  sensor.setIntegrationTime(50);
  sensor.setGain(3);
  sensor.setDrvCurrent(3);
  sensor.indicateLED(false);
  sensor.drvOff();

  showText("Sensors OK");
  delay(1000);
}

void loop()
{
  // ---------- TEMPERATURE ----------
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  bool tempOk = (tempC != DEVICE_DISCONNECTED_C);
  float tempF = 0.0f;

  if (tempOk) {
    tempF = tempC * 9.0f / 5.0f + 32.0f;
  }

  // ---------- COLOR SENSOR ----------
  sensor.drvOn();
  delay(60);

  sensor.startMeasurement();

  unsigned long start = millis();
  while (!sensor.dataReady()) {
    if (millis() - start > 1000) {
      sensor.drvOff();
      showText("Color timeout");
      delay(500);
      return;
    }
    delay(5);
  }

  float violet = sensor.readCalibratedViolet();
  float blue   = sensor.readCalibratedBlue();
  float green  = sensor.readCalibratedGreen();
  float yellow = sensor.readCalibratedYellow();
  float orange = sensor.readCalibratedOrange();
  float red    = sensor.readCalibratedRed();

  sensor.drvOff();

  const char* colorName = detectColor(violet, blue, green, yellow, orange, red);

  // ---------- OLED ----------
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(colorName);

  display.setTextSize(1);
  display.setCursor(0, 20);

  if (tempOk) {
    display.print("Temp: ");
    display.print(tempF, 1);
    display.println(" F");
  } else {
    display.println("Temp: ERR");
  }

  display.setCursor(0, 34);
  display.print("V:");
  display.print(violet, 0);
  display.print(" B:");
  display.print(blue, 0);

  display.setCursor(0, 44);
  display.print("G:");
  display.print(green, 0);
  display.print(" Y:");
  display.print(yellow, 0);

  display.setCursor(0, 54);
  display.print("O:");
  display.print(orange, 0);
  display.print(" R:");
  display.print(red, 0);

  display.display();

  delay(1000);
}