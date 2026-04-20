#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AS726x.h>

// ----------------------
// ESP32-C3 I2C Pins
// Change if needed for your board
// ----------------------
#define I2C_SDA 8
#define I2C_SCL 9

// ----------------------
// OLED Settings
// ----------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_AS726x sensor;

void showMessage(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Start I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Start OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed");
    while (1);
  }

  showMessage("Starting...", "OLED OK");

  // Start AS726x
  if (!sensor.begin(&Wire)) {
    showMessage("AS726x Failed", "Check Wiring");
    Serial.println("AS726x not found");
    while (1);
  }

  // Sensor setup
  sensor.setIntegrationTime(50);
  sensor.setGain(3);   // 64X gain (patched)
  sensor.drvOff();
  sensor.indicateLED(false);

  showMessage("Sensor Ready", "");
  delay(1000);
}

void loop() {

  sensor.startMeasurement();

  while (!sensor.dataReady()) {
    delay(5);
  }

  float violet = sensor.readCalibratedViolet();
  float blue   = sensor.readCalibratedBlue();
  float green  = sensor.readCalibratedGreen();
  float yellow = sensor.readCalibratedYellow();
  float orange = sensor.readCalibratedOrange();
  float red    = sensor.readCalibratedRed();

  // Serial output
  Serial.print("V:");
  Serial.print(violet, 1);
  Serial.print(" B:");
  Serial.print(blue, 1);
  Serial.print(" G:");
  Serial.print(green, 1);
  Serial.print(" Y:");
  Serial.print(yellow, 1);
  Serial.print(" O:");
  Serial.print(orange, 1);
  Serial.print(" R:");
  Serial.println(red, 1);

  // OLED output
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.print("V: "); display.println(violet, 1);
  display.print("B: "); display.println(blue, 1);
  display.print("G: "); display.println(green, 1);
  display.print("Y: "); display.println(yellow, 1);
  display.print("O: "); display.println(orange, 1);
  display.print("R: "); display.println(red, 1);

  display.display();

  delay(500);
}