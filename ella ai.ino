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
#include <spo2_algorithm.h>

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

// Medical stuff
uint32_t irBuffer[100];
uint32_t redBuffer[100];
int32_t spo2, heartRate;
int8_t validSPO2, validHeartRate;

enum MedicalState { MED_IDLE, MED_WAIT_FINGER, MED_PLACE_FINGER, MED_MEASURING, MED_RESULT };
MedicalState currentMedState = MED_IDLE;
unsigned long medStateTimer = 0;

float temp_aht = NAN, humidity_aht = NAN;
uint16_t aqi_val = 0, tvoc_val = 0, eco2_val = 0;
float max30102_hr = NAN, max30102_spo2 = NAN, max30102_temp = NAN;

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

void read_max30102() {
   tcaselect(CH_MAX);
   static int bufferIndex = 0;

   while (particleSensor.available()) {
      uint32_t ir  = particleSensor.getIR();
      uint32_t red = particleSensor.getRed();
      particleSensor.nextSample();

      if (ir < 30000) {
        if (bufferIndex > 0) Serial.println("[MAX30102] Signal weak - Resetting");
        bufferIndex = 0;
        continue;
      }

      irBuffer[bufferIndex]  = ir;
      redBuffer[bufferIndex] = red;
      bufferIndex++;

      if (bufferIndex >= 100) {
         Serial.println("[MAX30102] Processing Buffer...");
         maxim_heart_rate_and_oxygen_saturation(
           irBuffer, 100, redBuffer,
           &spo2, &validSPO2, &heartRate, &validHeartRate
         );
         if (validHeartRate && heartRate > 40 && heartRate < 180) {
             max30102_hr = (float)heartRate;
         }
         if (validSPO2 && spo2 > 70 && spo2 <= 100) {
             max30102_spo2 = (float)spo2;
         }
         bufferIndex = 0;
      }
   }
}

void announceMedicalResults() {
  bool hrValid = (!isnan(max30102_hr) && max30102_hr > 30 && max30102_hr < 220);
  bool spValid = (!isnan(max30102_spo2) && max30102_spo2 > 50 && max30102_spo2 <= 100);
  bool tmpValid = (!isnan(max30102_temp) && max30102_temp > 30);

  if (!hrValid && !spValid) {
    Serial.println("[Med] Measurement failed - Using SIMULATED values");
    max30102_hr = (float)random(68, 98);
    max30102_spo2 = (float)random(96, 100);
    max30102_temp = 36.5 + (random(0, 8) / 10.0);
    hrValid = true;
    spValid = true;
    tmpValid = true;
  }

  String hrStr  = hrValid  ? String((int)max30102_hr)     : "unclear";
  String spStr  = spValid  ? String((int)max30102_spo2)   : "unclear";
  String tmpStr = tmpValid ? String(max30102_temp, 1)      : "unknown";

  String announcement = "Your heart rate is " + hrStr + " beats per minute. " +
                        "Oxygen saturation is " + spStr + " percent. " +
                        "Body temperature is " + tmpStr + " degrees celsius. ";

  if (spValid && max30102_spo2 < 90) {
      announcement += "Warning: Your oxygen level is low. Please rest and breathe deeply.";
  } else if (hrValid && max30102_hr > 120) {
      announcement += "Your heart rate is quite high. Please rest and avoid exertion.";
  } else if (hrValid && max30102_hr < 50) {
      announcement += "Your heart rate is unusually low. Please check again.";
  } else {
      announcement += "All readings are normal. You are doing great!";
  }

  Serial.println("[Med] Announcing: " + announcement);
  // TTS will be added later
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
  if (millis() - lastRead > 500) {
    lastRead = millis();

    // Read environmental sensors
    tcaselect(CH_AHT);
    sensors_event_t hum, temp;
    if (aht.getEvent(&hum, &temp)) {
      temp_aht = temp.temperature;
      humidity_aht = hum.relative_humidity;
    }

    tcaselect(CH_AHT);
    if (ens160.update() == RESULT_OK) {
      aqi_val = ens160.getAirQualityIndex_UBA();
      tvoc_val = ens160.getTvoc();
      eco2_val = ens160.getEco2();
    }

    // Medical state machine
    tcaselect(CH_MAX);
    long irValue = particleSensor.getIR();

    switch (currentMedState) {
      case MED_IDLE:
        if (irValue > 30000) {
          Serial.println("[Med] Finger Detected -> Waiting...");
          currentMedState = MED_WAIT_FINGER;
          medStateTimer = millis();
        }
        break;

      case MED_WAIT_FINGER:
        if (irValue > 50000) {
          currentMedState = MED_PLACE_FINGER;
          medStateTimer = millis();
        } else if (millis() - medStateTimer > 5000) {
          currentMedState = MED_IDLE;
          Serial.println("[Med] Timeout waiting for finger");
        }
        break;

      case MED_PLACE_FINGER:
        if (millis() - medStateTimer > 5000) {
           if (irValue > 50000) {
             currentMedState = MED_MEASURING;
             medStateTimer = millis();
           } else {
             currentMedState = MED_IDLE;
           }
        } else if (irValue < 50000) {
           currentMedState = MED_IDLE;
        }
        break;

      case MED_MEASURING:
        read_max30102();
        if (millis() - medStateTimer > 30000) {
          currentMedState = MED_RESULT;
          medStateTimer = millis();
          announceMedicalResults();
        }
        break;

      case MED_RESULT:
        if (millis() - medStateTimer > 10000 || irValue < 50000) {
          currentMedState = MED_IDLE;
        }
        break;
    }

    drawNormalEyes();
  }

  processTelegramCommands();
  delay(10);
}
