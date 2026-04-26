#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 d(128, 64, &Wire, -1);
SMTPSession smtp;

#define WIFI_SSID "Leslie"
#define WIFI_PASSWORD "leslie04"

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

#define AUTHOR_EMAIL "garucialeshlie@gmail.com"
#define AUTHOR_PASSWORD "pteckqblkqursykl"   // no spaces

#define RECIPIENT_EMAIL "lesliegs409@gmail.com"

#define JOY_X 0      // A0 / GPIO0
#define JOY_Y 1      // A1 / GPIO1
#define JOY_BTN 2    // GPIO2

void msg(const char* a, const char* b = "") {
  d.clearDisplay();
  d.setCursor(0, 18);
  d.setTextSize(1);
  d.setTextColor(WHITE);
  d.println(a);
  d.println(b);
  d.display();
}

void fish(int x, int y) {
  // d.fillTriangle(x, y, x+7, y-4, x+7, y+4, WHITE);
  d.fillTriangle(x - 2, y, x + 7, y - 4, x + 24, y + 4, WHITE);
  d.fillRoundRect(x+7, y-5, 17, 10, 5, WHITE);
  d.fillCircle(x+20, y-2, 1, BLACK);
}

void sendMail() {
  msg("Sending email...");

  ESP_Mail_Session s;
  s.server.host_name = "smtp.gmail.com";
  s.server.port = 465;
  s.login.email = AUTHOR_EMAIL;
  s.login.password = AUTHOR_PASSWORD;

  SMTP_Message m;
  m.sender.name = "ESP32";
  m.sender.email = AUTHOR_EMAIL;
  m.subject = "ESP32 Alert";
  m.addRecipient("Leslie", RECIPIENT_EMAIL);
  m.text.content = "Este es Caramello y ESP32 dejandole saber que sus pescados necessitan ayuda. Por favor.";

  if (!smtp.connect(&s)) {
    msg("SMTP failed");
    return;
  }

  if (MailClient.sendMail(&smtp, &m)) msg("Email sent!", "Check inbox");
  else msg("Email failed");

  smtp.closeSession();
  delay(3000);
}

void setup() {
  d.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  d.clearDisplay();
  d.display();

  msg("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t++ < 40) delay(500);

  if (WiFi.status() != WL_CONNECTED) {
    msg("WiFi failed", "Check hotspot");
    while (1);
  }

  msg("WiFi connected!");
  delay(1000);
  sendMail();

  pinMode(JOY_X, INPUT);
  pinMode(JOY_Y, INPUT);
  pinMode(JOY_BTN, INPUT_PULLUP);

}

void showJoystick() {
  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);
  int btn = digitalRead(JOY_BTN);

  d.clearDisplay();
  d.setTextSize(1);
  d.setTextColor(WHITE);

  d.setCursor(0, 0);
  d.println("Joystick:");

  if (btn == LOW) {
    d.setCursor(38, 52);
    d.println("PRESS");
  }

    // Rotated mapping:
  // physical LEFT  -> OLED UP
  // physical UP    -> OLED RIGHT
  // physical RIGHT -> OLED DOWN
  // physical DOWN  -> OLED LEFT

  if (x < 1200) { // up
    d.setCursor(60, 18);
    d.println("^");
  } 
  else if (y < 1200) { // right
    d.setCursor(65, 28);
    d.println(">");
  } 
  else if (x > 2800) { // down
    d.setCursor(60, 38);
    d.println("v");
  } 
  else if (y > 2800) { // left
    d.setCursor(55, 28);
    d.println("<");
  } 
  else {
    fish(45, 32);
  }

  d.display();
}

void loop() {
  showJoystick();
  delay(120);
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