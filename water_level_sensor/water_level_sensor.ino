#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int WATER_SENSOR_PIN = 2;

void setup() {
  Serial.begin(115200);

  delay(1000);
  Serial.println("ESP32-C3 started!");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
}

void loop() {
  int value = analogRead(WATER_SENSOR_PIN);

  // Clear screen
  display.clearDisplay();

  // Title
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Water Level:");

  // Big value
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(value);

  // Status text
  display.setTextSize(1);
  display.setCursor(0, 50);

  if (value < 500) {
    display.println("Status: DRY");
  } 
  else if (value < 1500) {
    display.println("Status: LOW");
  } 
  else if (value < 3000) {
    display.println("Status: MEDIUM");
  } 
  else {
    display.println("Status: HIGH");
  }

  // Show everything
  display.display();

  delay(1000);
}