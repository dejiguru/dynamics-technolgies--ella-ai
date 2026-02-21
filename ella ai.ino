Step 4 – MAX30102 Fix (5:15 PM Saturday)
Commit: Fixed MAX30102 I2C address shift, reading raw IR data

cpp
#include <WiFi.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>
#include <MAX30105.h>

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

// Sensor objects
Adafruit_AHTX0 aht;
ENS160 ens160;
MAX30105 particleSensor;

// TCA9548A multiplexer
#define TCA_ADDR 0x70
#define CH_AHT  2
#define CH_ENS  2   // same as AHT
#define CH_MAX  4

void tcaselect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("EllaBox Starting...");

  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(20000000);
  tft.setRotation(0);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(20, 150);
  tft.print("ELLA BOOTING...");

  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.println("WiFi Started...");

  Wire.begin(8, 9);
  Serial.println("I2C Started");

  pinMode(TACTILE_SWITCH_PIN, INPUT_PULLUP);

  tcaselect(CH_AHT);
  if (!aht.begin()) Serial.println("AHT20 not found!");
  else Serial.println("AHT20 OK");

  tcaselect(CH_AHT);
  ens160.begin(&Wire, 0x53);
  if (!ens160.init()) Serial.println("ENS160 not found!");
  else {
    ens160.startStandardMeasure();
    Serial.println("ENS160 OK");
  }

  // MAX30102 FIXED
  tcaselect(CH_MAX);
  Wire.beginTransmission(0x57);
  if (Wire.endTransmission() != 0) {
      Serial.println("MAX30102 NOT DETECTED on I2C!");
  }

  if (!particleSensor.begin(Wire, 0x57)) {
    Serial.println("MAX30102 not found!");
  } else {
    Serial.println("MAX30102 OK");
    particleSensor.setup(0x3C, 8, 3, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x3C);
    particleSensor.setPulseAmplitudeIR(0x3C);
  }
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.println("Loop alive");
    lastPrint = millis();
  }

  static unsigned long lastIR = 0;
  if (millis() - lastIR > 1000) {
    tcaselect(CH_MAX);
    long ir = particleSensor.getIR();
    Serial.printf("IR value: %ld\n", ir);
    lastIR = millis();
  }
}
