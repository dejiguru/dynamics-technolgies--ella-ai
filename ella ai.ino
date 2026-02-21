

cpp
#include <WiFi.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>

// PINS
#define TFT_CS 10
#define TFT_DC 2
#define TFT_RST -1
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_MISO 13
#define TACTILE_SWITCH_PIN 38

// WiFi
const char* ssid = "ella";
const char* password = "12345678";

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("EllaBox Starting...");

  // Init SPI and TFT
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(20000000);
  tft.setRotation(0);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 150);
  tft.print("ELLA BOOTING...");

  // WiFi (non-blocking)
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.println("WiFi Started...");

  // I2C
  Wire.begin(8, 9);  // SDA=8, SCL=9
  Serial.println("I2C Started");

  pinMode(TACTILE_SWITCH_PIN, INPUT_PULLUP);
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.println("Loop alive");
    lastPrint = millis();
  }
}
void loop() 