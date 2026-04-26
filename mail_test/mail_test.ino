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
  d.fillTriangle(x, y, x+7, y-4, x+7, y+4, WHITE);
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
  m.text.content = "ESP32 test alert sent successfully.";

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
}

void loop() {
  for (int x = -30; x < 130; x += 5) {
    d.clearDisplay();
    d.setCursor(20, 5);
    d.println("Idle");
    fish(x, 32);
    fish(x - 45, 48);
    d.display();
    delay(130);
  }
}