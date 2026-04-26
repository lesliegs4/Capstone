#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_AS726x.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define I2C_SDA 8
#define I2C_SCL 9

#define WATER_SENSOR_PIN 2
#define JOY_X 0
#define JOY_Y 1
#define JOY_BTN 2
#define TEMP_PIN 4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define TDS_PIN 4
#define VREF 3.3
#define SCOUNT 30

#define WIFI_SSID "Leslie"
#define WIFI_PASSWORD "leslie04"

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

#define AUTHOR_EMAIL "garucialeshlie@gmail.com"
#define AUTHOR_PASSWORD "pteckqblkqursykl"

#define RECIPIENT_EMAIL "lesliegs409@gmail.com"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_AS726x colorSensor;
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
SMTPSession smtp;

int screen = 0;
bool colorOK = false;
bool emailSent = false;
unsigned long lastMove = 0;

int tdsBuffer[SCOUNT];
int tdsIndex = 0;
float tdsValue = 0;

void setup() {
  Wire.begin(I2C_SDA, I2C_SCL);

  pinMode(JOY_X, INPUT);
  pinMode(JOY_Y, INPUT);
  pinMode(JOY_BTN, INPUT_PULLUP);

  pinMode(TDS_PIN, INPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);

  tempSensor.begin();

  colorOK = colorSensor.begin(&Wire);
  if (colorOK) {
    colorSensor.setIntegrationTime(50);
    colorSensor.setGain(3);
    colorSensor.setDrvCurrent(3);
    colorSensor.indicateLED(false);
    colorSensor.drvOff();
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  handleJoystick();

  if (screen == 0) showWaterScreen();
  else if (screen == 1) showColorTempScreen();
  else if (screen == 2) showTDSScreen();
  else if (screen == 3) showWiFiScreen();
  

  delay(150);
}

void handleJoystick() {
  if (millis() - lastMove < 500) return;

  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  bool pressed = digitalRead(JOY_BTN) == LOW;

  // rotated mapping from your previous setup:
  // physical UP gives ">"
  if (y < 1200) {
    screen++;
    if (screen > 2) screen = 0;
    lastMove = millis();
  }

  // physical DOWN gives "<"
  if (y > 2800) {
    screen--;
    if (screen < 0) screen = 3;
    lastMove = millis();
  }

  if (pressed && screen == 3) {
    sendEmail();
    lastMove = millis();
  }
}

void showWaterScreen() {
  int value = analogRead(WATER_SENSOR_PIN);

  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Screen 1: Water");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(value);

  display.setTextSize(1);
  display.setCursor(0, 50);

  if (value < 500) display.println("Status: DRY");
  else if (value < 1500) display.println("Status: LOW");
  else if (value < 3000) display.println("Status: MEDIUM");
  else display.println("Status: HIGH");

  display.display();
}

const char* detectColor(float v, float b, float g, float y, float o, float r) {
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

void showColorTempScreen() {
  display.clearDisplay();

  if (!colorOK) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Screen 2: Color");
    display.println("AS726x FAIL");
    display.println("Check wiring");
    display.display();
    return;
  }

  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  float tempF = tempC * 9.0 / 5.0 + 32.0;

  colorSensor.drvOn();
  delay(60);
  colorSensor.startMeasurement();

  unsigned long start = millis();
  while (!colorSensor.dataReady()) {
    if (millis() - start > 1000) {
      colorSensor.drvOff();
      display.setCursor(0, 0);
      display.println("Color timeout");
      display.display();
      return;
    }
    delay(5);
  }

  float violet = colorSensor.readCalibratedViolet();
  float blue   = colorSensor.readCalibratedBlue();
  float green  = colorSensor.readCalibratedGreen();
  float yellow = colorSensor.readCalibratedYellow();
  float orange = colorSensor.readCalibratedOrange();
  float red    = colorSensor.readCalibratedRed();

  colorSensor.drvOff();

  const char* colorName = detectColor(violet, blue, green, yellow, orange, red);

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(colorName);

  display.setTextSize(1);
  display.setCursor(0, 20);

  if (tempC == DEVICE_DISCONNECTED_C) {
    display.println("Temp: ERR");
  } else {
    display.print("Temp: ");
    display.print(tempF, 1);
    display.println(" F");
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
}

void showTDSScreen() {
  int raw = analogRead(TDS_PIN);
  float voltage = raw * VREF / 4095.0;

  // Use temp compensation if DS18B20 is connected
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED_C) tempC = 25.0;

  float compensation = 1.0 + 0.02 * (tempC - 25.0);
  float compensatedVoltage = voltage / compensation;

  tdsValue =
    (133.42 * compensatedVoltage * compensatedVoltage * compensatedVoltage
    - 255.86 * compensatedVoltage * compensatedVoltage
    + 857.39 * compensatedVoltage) * 0.5;

  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Screen 3: TDS");

  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(tdsValue, 0);
  display.println("ppm");

  display.setTextSize(1);
  display.setCursor(0, 45);
  display.print("Raw: ");
  display.print(raw);

  display.setCursor(0, 55);
  if (tdsValue < 150) display.println("Status: CLEAN");
  else if (tdsValue < 500) display.println("Status: OK");
  else display.println("Status: HIGH");

  display.display();
}

void showWiFiScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("Screen 4: WiFi/Mail");

  if (WiFi.status() == WL_CONNECTED) {
    display.println("WiFi: Connected");
    display.print("IP: ");
    display.println(WiFi.localIP());
  } else {
    display.println("WiFi: Connecting...");
  }

  display.println();
  display.println("Press joystick");
  display.println("to send email");

  if (emailSent) display.println("Email: SENT");
  else display.println("Email: not sent");

  display.display();
}

void sendEmail() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sending email...");
  display.display();

  if (WiFi.status() != WL_CONNECTED) {
    display.println("WiFi not ready");
    display.display();
    return;
  }

  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;

  SMTP_Message msg;
  msg.sender.name = "ESP32 Tank";
  msg.sender.email = AUTHOR_EMAIL;
  msg.subject = "ESP32 Tank Test";
  msg.addRecipient("Leslie", RECIPIENT_EMAIL);
  msg.text.content = "ESP32 tank monitor test email sent successfully.";

  if (!smtp.connect(&session)) {
    emailSent = false;
    return;
  }

  emailSent = MailClient.sendMail(&smtp, &msg);
  smtp.closeSession();
}

// void loop() {
//   for (int x = -30; x < 130; x += 5) {
//     d.clearDisplay();
//     d.setCursor(20, 5);
//     d.println("Idle");
//     fish(x, 32);
//     fish(x - 45, 48);
//     d.display();
//     delay(130);
//   }
// }