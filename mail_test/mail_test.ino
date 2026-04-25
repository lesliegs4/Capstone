#include <WiFi.h>
#include <ESP_Mail_Client.h>

#define WIFI_SSID "Leslie"
#define WIFI_PASSWORD "leslie04"

#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

#define AUTHOR_EMAIL "garucialeshlie@gmail.com"
#define AUTHOR_PASSWORD "pteckqblkqursykl"   // no spaces

#define RECIPIENT_EMAIL "lesliegs409@gmail.com"

SMTPSession smtp;

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");

  sendEmail();
}

void loop() {}

void sendEmail() {

  ESP_Mail_Session session;

  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;

  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;

  SMTP_Message message;

  message.sender.name = "ESP32";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "ESP32 Test";
  message.addRecipient("Leslie", RECIPIENT_EMAIL);

  message.text.content = "This is a direct Gmail test from ESP32! Este es el segundo correo";
  message.text.charSet = "us-ascii";

  if (!smtp.connect(&session)) {
    Serial.println("SMTP failed");
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println(smtp.errorReason());
  } else {
    Serial.println("Email sent!");
  }

  smtp.closeSession();
}