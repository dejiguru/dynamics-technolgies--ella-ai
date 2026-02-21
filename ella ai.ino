<<<<<<< HEAD
=======

>>>>>>> 0ceeec48716727e0c264833cc6147db54411c856
#include <WiFi.h>
#include <SPIFFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS16x.h>
#include <MAX30105.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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
#define CH_EYE_LEFT  0
#define CH_EYE_RIGHT 1

void tcaselect(uint8_t i) {
  if (i > 7) return;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << i);
  Wire.endTransmission();
}

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void drawNormalEyes() {
  for (int i = 0; i < 2; i++) {
    tcaselect(i == 0 ? CH_EYE_LEFT : CH_EYE_RIGHT);
    display.clearDisplay();
    display.fillCircle(64, 32, 26, SSD1306_WHITE);
    display.fillCircle(64, 32, 10, SSD1306_BLACK);
    display.fillCircle(70, 26, 3, SSD1306_WHITE);
    display.display();
  }
}

// Telegram globals
String cloudBotToken = "";
String cloudChatId = "";
int lastUpdateId = 0;

void processTelegramCommands() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 10000) return;
  lastCheck = millis();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://api.telegram.org/bot" + cloudBotToken + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=1";
  if (!http.begin(client, url)) return;

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return; }

  String payload = http.getString();
  http.end();

  // BUG: using StaticJsonDocument<128> – too small for typical response
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("Telegram JSON parse failed");
    return;
  }

  JsonArray results = doc["result"].as<JsonArray>();
  if (results.size() > 0) {
    lastUpdateId = results[0]["update_id"];
    String text = results[0]["message"]["text"];
    Serial.println("Got command: " + text);
  }
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

  tcaselect(CH_EYE_LEFT);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Left Eye Failed");
  } else {
    Serial.println("Left Eye OK");
    display.clearDisplay();
    display.display();
  }

  tcaselect(CH_EYE_RIGHT);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Right Eye Failed");
  } else {
    Serial.println("Right Eye OK");
    display.clearDisplay();
    display.display();
  }
}

void loop() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 500) {   // throttle to 500ms
    lastRead = millis();

    tcaselect(CH_AHT);
    sensors_event_t hum, temp;
    if (aht.getEvent(&hum, &temp)) {
      Serial.printf("Temp: %.1f  Hum: %.1f\n", temp.temperature, hum.relative_humidity);
    }

    tcaselect(CH_MAX);
    long ir = particleSensor.getIR();
    Serial.printf("IR: %ld\n", ir);

    tcaselect(CH_AHT);
    if (ens160.update() == RESULT_OK) {
      Serial.printf("AQI: %d\n", ens160.getAirQualityIndex_UBA());
    }

    drawNormalEyes();
  }

  processTelegramCommands();   // runs every 10s internally
  delay(10);   // yield to idle task
}
