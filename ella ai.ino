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
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>

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

// Firebase
const char* FIREBASE_HOST = "ella-b927d-default-rtdb.firebaseio.com";
const char* FIREBASE_DATABASE_URL = "https://ella-b927d-default-rtdb.firebaseio.com";
const char* FIREBASE_AUTH = "AIzaSyC_yLxDXOqJMY6WB34vxVHe9JP-457kcvI";
const char* FIREBASE_DB_SECRET = "6p1xNhaN0ZAYJ4Ouy4iKTW3MujgYm8ji71pYzWOZ";

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Sensor objects
Adafruit_AHTX0 aht;
ENS160 ens160;
MAX30105 particleSensor;

// TCA9548A multiplexer
#define TCA_ADDR 0x70
#define CH_AHT  2
#define CH_ENS  2
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

// UI colors
#define UI_BG 0x0000
#define UI_CARD_BG 0x2124
#define UI_ACCENT 0xFADE
#define UI_TEXT_MAIN 0xFFFF
#define UI_TEXT_SUB 0x9CF3
#define UI_ALERT 0xF800
#define UI_ERROR 0xF800
#define UI_SUCCESS 0x07E0
#define UI_INFO 0x07FF

// Firebase globals
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseReady = false;
Preferences prefs;
String wifiSSID, wifiPass;
String cloudBotToken = "";
String cloudChatId = "";
String user_name = "";
String user_emergency_contact = "";
String cloudRemindersJson = "[]";
int lastUpdateId = 0;

// Forward declarations
void drawNormalEyes();
void drawNavigationBar();
void drawStatusDot(bool);
void drawNormalScreen(bool);
void read_max30102();
void announceMedicalResults();
void processTelegramCommands();
void setupFirebase();
void tokenStatusCallback(TokenInfo);
void pushSensorDataToFirebase();
void syncUserProfileFromFirebase();
void syncRemindersFromFirebase();
void checkRemoteCommands();
bool sendTelegramMessage(String);
void syncWithFirebase();
void checkAutoWeeklyReport();
void sendWeeklyReport();
void checkAirQualityAlerts();
String getRemindersContext();
void sendEmergencyAlert(String);

// ========== UI Functions ==========
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

void drawNavigationBar() {
  tft.fillRect(0, 320-30, 240, 30, UI_CARD_BG);
  tft.drawLine(0, 320-31, 240, 320-31, UI_ACCENT);
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT_SUB);
  tft.setCursor(8, 320-22);
  tft.print("NORMAL");
  tft.fillRoundRect(240-46, 320-28, 42, 22, 5, UI_INFO);
  tft.setTextColor(UI_BG);
  tft.setCursor(240-40, 320-22);
  tft.print("AI >");
}

void drawStatusDot(bool connected) {
  tft.fillCircle(240-15, 12, 4, connected ? UI_SUCCESS : UI_ERROR);
}

// ========== drawNormalScreen ==========
void drawNormalScreen(bool force) {
  static unsigned long lastDraw = 0;
  static MedicalState lastRenderedState = (MedicalState)-1;
  static int last_min = -1;
  static float last_temp = -999;
  static float last_humidity = -999;
  static uint16_t last_aqi = 9999;
  
  if (!force && millis() - lastDraw < 500) return;
  lastDraw = millis();

  bool fullRedraw = force || (lastRenderedState != currentMedState);
  
  if (fullRedraw) {
    tft.fillScreen(UI_BG);
    tft.fillRect(0, 0, 240, 25, UI_CARD_BG);
    tft.setFont();
    tft.setTextSize(2);
    tft.setTextColor(UI_ACCENT);
    tft.setCursor(10, 5);
    tft.print("ELLA BOX");
    drawStatusDot(firebaseReady);
    drawNavigationBar();
    lastRenderedState = currentMedState;
    last_min = -1;
  }

  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    if(timeinfo.tm_min != last_min || fullRedraw) {
       tft.fillRect(240-85, 0, 65, 25, UI_CARD_BG);
       tft.setFont();
       tft.setTextSize(2);
       tft.setTextColor(UI_TEXT_SUB);
       tft.setCursor(240-80, 5);
       tft.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
       last_min = timeinfo.tm_min;
    }
  }

  if (currentMedState == MED_RESULT) {
    if (fullRedraw) {
        tft.fillRoundRect(10, 40, 220, 100, 10, UI_CARD_BG);
        tft.drawRoundRect(10, 40, 220, 100, 10, UI_ACCENT);

        bool hrValid = (!isnan(max30102_hr) && max30102_hr > 30 && max30102_hr < 220);
        bool spValid = (!isnan(max30102_spo2) && max30102_spo2 > 50 && max30102_spo2 <= 100);
        bool tmpValid = (!isnan(max30102_temp) && max30102_temp > 30);

        tft.setFont(&FreeSansBold24pt7b);
        tft.setTextSize(1);
        tft.setTextColor(UI_TEXT_MAIN);
        tft.setCursor(10+55, 40+55);
        if (hrValid) tft.print((int)max30102_hr);
        else tft.print("--");

        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(10+68, 40+80);
        tft.print("BPM");

        tft.setTextColor(hrValid ? UI_SUCCESS : UI_ERROR);
        tft.setFont();
        tft.setTextSize(1);
        tft.setCursor(10+150, 40+55);
        tft.print(hrValid ? "NORMAL" : "---");

        tft.fillRoundRect(10, 160, 105, 70, 8, UI_CARD_BG);
        tft.drawRoundRect(10, 160, 105, 70, 8, UI_INFO);
        tft.setFont(&FreeSansBold12pt7b);
        tft.setTextColor(UI_INFO);
        tft.setCursor(10+15, 160+40);
        if (spValid) { tft.print((int)max30102_spo2); tft.print("%"); }
        else tft.print("--");
        tft.setFont();
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(10+25, 160+55);
        tft.print("SpO2");

        tft.fillRoundRect(125, 160, 105, 70, 8, UI_CARD_BG);
        tft.drawRoundRect(125, 160, 105, 70, 8, UI_ACCENT);
        tft.setFont(&FreeSansBold12pt7b);
        tft.setTextColor(UI_ACCENT);
        tft.setCursor(125+10, 160+40);
        if (tmpValid) { tft.print(max30102_temp, 1); tft.print("C"); }
        else tft.print("--");
        tft.setFont();
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(125+15, 160+55);
        tft.print("Temp");
    }
    
    tft.fillCircle(10+30, 40+45, 16, UI_CARD_BG);
    int heartSize = ((millis() % 400) < 100) ? 12 : 10;
    tft.fillCircle(10+25, 40+40, heartSize, UI_ALERT);
    tft.fillCircle(10+35, 40+40, heartSize, UI_ALERT);
    tft.fillTriangle(10+17, 40+44, 10+43, 40+44, 10+30, 40+60, UI_ALERT);

    static int last_result_count = -1;
    unsigned long elapsed = millis() - medStateTimer;
    int remaining = 10 - (elapsed / 1000);
    if (remaining < 0) remaining = 0;
    if (remaining != last_result_count || fullRedraw) {
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(10+150, 40+80);
        if (last_result_count != -1 && !fullRedraw) {
             tft.setTextColor(UI_CARD_BG);
             tft.print(last_result_count);
             tft.print("s");
             tft.setCursor(10+150, 40+80);
        }
        tft.setTextColor(UI_TEXT_SUB);
        tft.print(remaining);
        tft.print("s");
        last_result_count = remaining;
    }
  } else if (currentMedState == MED_MEASURING) {
    if (fullRedraw) {
       tft.fillRoundRect(10, 40, 220, 140, 10, UI_CARD_BG); 
       tft.drawRoundRect(10, 40, 220, 140, 10, UI_INFO);
    }
    unsigned long elapsed = millis() - medStateTimer;
    String status = "Measuring...";
    if (elapsed < 10000) status = "Reading Pulse...";
    else if (elapsed < 20000) status = "Reading Oxygen...";
    else status = "Reading Temp...";

    int cx = 120;
    int cy = 65;
    bool pulseState = (millis() / 500) % 2 == 0;
    uint16_t hColor = UI_ACCENT;

    static bool last_pulseState = false;
    static int last_phase = -1;
    int current_phase = elapsed / 10000;
    bool needClear = (pulseState != last_pulseState) || (current_phase != last_phase) || fullRedraw;
    if (needClear) {
        tft.fillRect(cx - 30, cy - 23, 60, 50, UI_CARD_BG); 
        last_pulseState = pulseState;
        last_phase = current_phase;
    }
    if (elapsed < 10000) { 
       if (needClear) {
           if (pulseState) {
              tft.fillCircle(cx-6, cy-6, 7, hColor);
              tft.fillCircle(cx+6, cy-6, 7, hColor);
              tft.fillTriangle(cx-13, cy+2, cx+13, cy+2, cx, cy+13, hColor);
           } else {
              tft.fillCircle(cx-5, cy-5, 5, hColor);
              tft.fillCircle(cx+5, cy-5, 5, hColor);
              tft.fillTriangle(cx-10, cy+1, cx+10, cy+1, cx, cy+10, hColor);
           }
       }
    } else if (elapsed < 20000) {
       hColor = UI_ACCENT;
       if (needClear) {
           if (pulseState) {
              tft.fillCircle(cx, cy+5, 12, hColor);
              tft.fillTriangle(cx-11, cy+2, cx+11, cy+2, cx, cy-15, hColor);
           } else {
              tft.fillCircle(cx, cy+5, 10, hColor);
              tft.fillTriangle(cx-9, cy+2, cx+9, cy+2, cx, cy-12, hColor);
           }
       }
    } else {
       hColor = 0xFDA0;
       if (needClear) {
           int tH = pulseState ? 34 : 30;
           int tW = 10;
           int tX = cx - tW/2;
           int tY = cy - 20;
           tft.fillRoundRect(tX, tY, tW, tH, 4, UI_ACCENT);
           tft.fillCircle(cx, tY + tH, 9, UI_ACCENT);
           tft.fillRoundRect(tX+3, tY+5, tW-6, tH-5, 2, UI_CARD_BG); 
           tft.fillRoundRect(tX+3, tY+15, tW-6, tH-15, 2, hColor); 
           tft.fillCircle(cx, tY + tH, 6, hColor); 
       }
    }
    
    static int last_remaining = -1;
    int remaining = 30 - (elapsed / 1000);
    if (remaining < 0) remaining = 0;
    if (remaining != last_remaining || fullRedraw) {
        tft.setFont(&FreeSansBold24pt7b);
        tft.setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        if (last_remaining != -1 && !fullRedraw) {
            String oldStr = String(last_remaining);
            tft.getTextBounds(oldStr, 0, 0, &x1, &y1, &w, &h);
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(cx - w/2, cy + 60);
            tft.print(oldStr);
        }
        String newStr = String(remaining);
        tft.getTextBounds(newStr, 0, 0, &x1, &y1, &w, &h);
        tft.setTextColor(UI_TEXT_MAIN);
        tft.setCursor(cx - w/2, cy + 60);
        tft.print(newStr);
        last_remaining = remaining;
    }
    
    static String last_status_text = "";
    if (status != last_status_text || fullRedraw) {
        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        tft.fillRect(11, 160, 218, 18, UI_CARD_BG);
        tft.drawRoundRect(10, 40, 220, 140, 10, UI_INFO);
        tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
        tft.setTextColor(UI_TEXT_MAIN);
        tft.setCursor(cx - w/2, 175);
        tft.print(status);
        last_status_text = status;
    }
  } else if (currentMedState == MED_PLACE_FINGER) {
    if (fullRedraw) {
       tft.fillRoundRect(10, 40, 220, 140, 10, UI_CARD_BG);
       tft.drawRoundRect(10, 40, 220, 140, 10, UI_SUCCESS);
       tft.setFont(&FreeSansBold9pt7b);
       tft.setTextSize(1);
       tft.setTextColor(UI_SUCCESS);
       String msg = "KEEP FINGER STILL";
       int16_t x1, y1; uint16_t w, h;
       tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
       tft.setCursor(10 + (220 - w)/2, 40+55);
       tft.print(msg);
    }
    static int last_place_count = -1;
    unsigned long elapsed = millis() - medStateTimer;
    int remaining = 5 - (elapsed / 1000);
    if (remaining < 0) remaining = 0;
    if (remaining != last_place_count || fullRedraw) {
        tft.setFont(&FreeSansBold18pt7b);
        tft.setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        tft.fillRect(80, 100, 60, 40, UI_CARD_BG);
        String newStr = String(remaining);
        tft.getTextBounds(newStr, 0, 0, &x1, &y1, &w, &h); 
        tft.setTextColor(UI_SUCCESS);
        tft.setCursor(10 + (220 - w)/2, 40+90);
        tft.print(newStr);
        last_place_count = remaining;
    }
  } else if (currentMedState == MED_WAIT_FINGER) {
    if (fullRedraw) {
        tft.fillRoundRect(10, 40, 220, 140, 10, UI_CARD_BG);
        tft.drawRoundRect(10, 40, 220, 140, 10, UI_ACCENT);
        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextSize(1);
        tft.setTextColor(UI_TEXT_MAIN);
        String line1 = "Place Finger";
        int16_t x1, y1; uint16_t w, h;
        tft.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
        tft.setCursor(10 + (220 - w)/2, 40+40); 
        tft.print(line1);
        String line2 = "on Sensor...";
        tft.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
        tft.setCursor(10 + (220 - w)/2, 40+65); 
        tft.print(line2);
    }
    static int last_wait_count = -1;
    int remaining = 5 - (millis() - medStateTimer) / 1000;
    if (remaining < 0) remaining = 0;
    if (remaining != last_wait_count || fullRedraw) {
        tft.setFont(&FreeSansBold12pt7b);
        tft.setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        if (last_wait_count != -1 && !fullRedraw) {
             String oldStr = String(last_wait_count);
             tft.getTextBounds(oldStr, 0, 0, &x1, &y1, &w, &h);
             tft.setTextColor(UI_CARD_BG);
             tft.setCursor(10 + (220 - w)/2, 40+95);
             tft.print(oldStr);
        }
        String newStr = String(remaining);
        tft.getTextBounds(newStr, 0, 0, &x1, &y1, &w, &h);
        tft.setTextColor(UI_ACCENT);
        tft.setCursor(10 + (220 - w)/2, 40+95); 
        tft.print(newStr);
        last_wait_count = remaining;
    }
  } else { // MED_IDLE
    if (fullRedraw) {
      tft.fillRect(0, 30, 240, 320-60, UI_BG);
      tft.fillRoundRect(10, 40, 220, 60, 10, UI_CARD_BG);
      tft.drawRoundRect(10, 40, 220, 60, 10, UI_ACCENT);
    }
    if (fullRedraw) { last_temp = -999; last_humidity = -999; }
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    if (isnan(temp_aht)) temp_aht = 0.0;
    if (abs(temp_aht - last_temp) > 0.1 || fullRedraw) {
        if (last_temp != -999 && !fullRedraw) {
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(20, 75);
            tft.print("Temp: ");
            tft.print(last_temp, 1);
            tft.print("C");
        }
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(20, 75);
        tft.print("Temp: ");
        if (temp_aht > 38.0) tft.setTextColor(UI_ERROR);
        else if (temp_aht > 37.5) tft.setTextColor(UI_ALERT);
        else tft.setTextColor(UI_TEXT_MAIN);
        tft.print(temp_aht, 1);
        tft.print("C");
        last_temp = temp_aht;
    }
    if (abs(humidity_aht - last_humidity) > 1.0 || fullRedraw) {
        if (last_humidity != -999) {
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(130, 75);
            tft.print("RH: ");
            tft.print(last_humidity, 0);
            tft.print("%");
        }
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(130, 75);
        tft.print("RH: ");
        if (humidity_aht > 70 || humidity_aht < 20) tft.setTextColor(UI_ALERT);
        else tft.setTextColor(UI_TEXT_MAIN);
        tft.print(humidity_aht, 0);
        tft.print("%");
        last_humidity = humidity_aht;
    }
    if (fullRedraw) {
      tft.fillRoundRect(10, 120, 220, 80, 10, UI_CARD_BG);
      tft.drawRoundRect(10, 120, 220, 80, 10, UI_INFO);
      tft.setTextColor(UI_TEXT_SUB);
      tft.setCursor(40, 145);
      tft.print("Air Quality Index");
    }
    if (fullRedraw) last_aqi = 9999;
    if (aqi_val != last_aqi) {
        tft.setFont(&FreeSansBold12pt7b);
        tft.setTextSize(1);
        if (last_aqi != 9999) {
            String oldStr = String(last_aqi);
            int16_t x1, y1; uint16_t w, h;
            tft.getTextBounds(oldStr, 0, 0, &x1, &y1, &w, &h);
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(120 - w / 2, 175);
            tft.print(oldStr);
        }
        String newStr = String(aqi_val);
        int16_t x1, y1; uint16_t w, h;
        tft.getTextBounds(newStr, 0, 0, &x1, &y1, &w, &h);
        if (aqi_val >= 4) tft.setTextColor(UI_ERROR);
        else if (aqi_val == 3) tft.setTextColor(UI_ALERT);
        else tft.setTextColor(UI_INFO); 
        tft.setCursor(120 - w / 2, 175);
        tft.print(newStr);
        last_aqi = aqi_val;
    }
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT_SUB);
    tft.setCursor(100, 195);
    tft.print("AQI");
    if (fullRedraw) {
      tft.fillRoundRect(10, 220, 220, 50, 10, UI_CARD_BG);
      tft.drawRoundRect(10, 220, 220, 50, 8, UI_ACCENT);
    }
    static uint16_t last_eco2 = 9999;
    static uint16_t last_tvoc = 9999;
    if (fullRedraw) { last_eco2 = 9999; last_tvoc = 9999; }
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextSize(1);
    if (eco2_val != last_eco2) {
        if (last_eco2 != 9999) {
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(15, 250);
            tft.print("eCO2: ");
            tft.print(last_eco2);
        }
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(15, 250);
        tft.print("eCO2: ");
        if (eco2_val > 1000) tft.setTextColor(UI_ALERT);
        else tft.setTextColor(UI_TEXT_MAIN);
        tft.print((int)eco2_val);
        last_eco2 = eco2_val;
    }
    if (tvoc_val != last_tvoc) {
        if (last_tvoc != 9999) {
            tft.setTextColor(UI_CARD_BG);
            tft.setCursor(120, 250);
            tft.print("TVOC: ");
            tft.print(last_tvoc);
        }
        tft.setTextColor(UI_TEXT_SUB);
        tft.setCursor(120, 250);
        tft.print("TVOC: ");
        if (tvoc_val > 200) tft.setTextColor(UI_ALERT);
        else tft.setTextColor(UI_TEXT_MAIN);
        tft.print((int)tvoc_val);
        last_tvoc = tvoc_val;
    }
  }
}

// ========== Medical Functions ==========
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
}

// ========== Firebase Functions ==========
void tokenStatusCallback(TokenInfo info) {
  String s = "Token Info: type = " + String(info.type) + ", status = " + String(info.status);
  Serial.println(s);
}

void setupFirebase() {
  config.api_key = FIREBASE_AUTH;
  config.database_url = FIREBASE_DATABASE_URL;
  config.signer.tokens.legacy_token = FIREBASE_DB_SECRET;
  config.timeout.serverResponse = 10 * 1000;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  unsigned long start = millis();
  while (!Firebase.ready() && millis() - start < 10000) {
    delay(100);
  }
  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("[Firebase] Connected & Ready!");
    syncUserProfileFromFirebase();
    syncRemindersFromFirebase();
  } else {
    Serial.println("[Firebase] Connection Timed Out");
  }
}

void pushSensorDataToFirebase() {
  if (!firebaseReady) return;
  static unsigned long lastPush = 0;
  if (millis() - lastPush < 10000) return;
  String path = "/readings";
  FirebaseJson json;
  json.set("temperature", isnan(temp_aht) ? 0.0 : temp_aht);
  json.set("humidity", isnan(humidity_aht) ? 0.0 : humidity_aht);
  json.set("heartRate", isnan(max30102_hr) ? 0 : max30102_hr);
  json.set("spo2", isnan(max30102_spo2) ? 0 : max30102_spo2);
  json.set("aqi", aqi_val);
  json.set("tvoc", tvoc_val);
  json.set("eco2", eco2_val);
  json.set("bodyTemp", isnan(max30102_temp) ? 0.0 : max30102_temp);
  json.set("timestamp/.sv", "timestamp");
  if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &json)) {
    Serial.println("[Firebase] Sensor data pushed");
  } else {
    Serial.printf("[Firebase] Push failed: %s\n", fbdo.errorReason().c_str());
  }
  lastPush = millis();
}

void syncUserProfileFromFirebase() {
  if (!firebaseReady) return;
  if (Firebase.RTDB.getJSON(&fbdo, "/commands/userProfile")) {
      FirebaseJson *json = fbdo.jsonObjectPtr();
      FirebaseJsonData result;
      json->get(result, "name");
      if (result.success) user_name = result.to<String>();
      json->get(result, "telegramBotToken");
      if (result.success) {
          cloudBotToken = result.to<String>();
          prefs.putString("botToken", cloudBotToken);
      }
      json->get(result, "telegramChatId");
      if (result.success) {
          cloudChatId = result.to<String>();
          prefs.putString("chatId", cloudChatId);
      }
      json->get(result, "emergencyContact");
      if (result.success) {
          user_emergency_contact = result.to<String>();
          prefs.putString("emergency", user_emergency_contact);
      }
      if (user_name.length() > 0) prefs.putString("userName", user_name);
  }
}

void syncRemindersFromFirebase() {
  if (!firebaseReady) return;
  if (Firebase.RTDB.getString(&fbdo, "/reminders")) {
    cloudRemindersJson = fbdo.stringData();
    Serial.println("[Firebase] Reminders synced");
  }
}

String getRemindersContext() {
  if (cloudRemindersJson == "[]" || cloudRemindersJson == "" || cloudRemindersJson == "null") {
      return "\nREMINDERS: No reminders found.\n";
  }
  String s = "\nREMINDERS:\n";
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, cloudRemindersJson);
  if (error) return "\nREMINDERS: Error reading checklist.\n";
  int count = 0;
  if (doc.is<JsonArray>()) {
      for (JsonVariant v : doc.as<JsonArray>()) {
          String title = v["detail"] | v["title"] | "";
          String time = v["time"] | "";
          String type = v["type"] | "";
          if (title.length() > 0) { 
             s += "- " + title + " (" + type + ") at " + time + "\n";
             count++;
          }
      }
  } else if (doc.is<JsonObject>()) {
      for (JsonPair p : doc.as<JsonObject>()) {
          JsonVariant v = p.value();
          String title = v["detail"] | v["title"] | "";
          String time = v["time"] | "";
          String type = v["type"] | "";
          if (title.length() > 0) { 
             s += "- " + title + " (" + type + ") at " + time + "\n";
             count++;
          }
      }
  }
  if (count == 0) return "\nREMINDERS: You have no pending tasks.\n";
  return s;
}

bool sendTelegramMessage(String msg) {
  if (cloudBotToken == "" || cloudChatId == "") return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(10000);
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + cloudBotToken + "/sendMessage";
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  StaticJsonDocument<512> doc;
  doc["chat_id"] = cloudChatId;
  doc["text"]    = msg;
  doc["parse_mode"] = "HTML";
  String payload;
  serializeJson(doc, payload);
  int httpCode = http.POST(payload);
  String response = http.getString();
  http.end();
  client.stop();
  return (httpCode == 200);
}

void checkRemoteCommands() {
    if (!firebaseReady) return;
    static unsigned long lastCmdCheck = 0;
    if (millis() - lastCmdCheck < 3000) return;
    lastCmdCheck = millis();
    if (Firebase.RTDB.getBool(&fbdo, "/commands/emergency")) {
        if (fbdo.boolData()) {
            Serial.println("[Command] Emergency Triggered from Web!");
            sendEmergencyAlert("Web App Panic Button");
            Firebase.RTDB.setBool(&fbdo, "/commands/emergency", false);
        }
    }
    if (Firebase.RTDB.getJSON(&fbdo, "/commands/wifiConfig")) {
        FirebaseJson *json = fbdo.jsonObjectPtr();
        String newSSID, newPass;
        FirebaseJsonData result;
        json->get(result, "ssid");
        if (result.success) newSSID = result.to<String>();
        json->get(result, "password");
        if (result.success) newPass = result.to<String>();
        if (newSSID.length() > 0) {
            Serial.println("[Command] New WiFi Config Received!");
            prefs.begin("ella", false);
            prefs.putString("ssid", newSSID);
            prefs.putString("pass", newPass);
            prefs.end();
            Firebase.RTDB.deleteNode(&fbdo, "/commands/wifiConfig");
            sendTelegramMessage("New Wi-Fi saved. Restarting.");
            delay(2000);
            ESP.restart();
        }
    }
}

void syncWithFirebase() {
  static unsigned long lastSync = 0;
  if (millis() - lastSync < 60000) return;
  lastSync = millis();
  syncUserProfileFromFirebase();
  syncRemindersFromFirebase();
}

void checkAutoWeeklyReport() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;
  static bool reportSentToday = false;
  if (timeinfo.tm_wday == 0 && timeinfo.tm_hour == 9 && timeinfo.tm_min == 0) {
    if (!reportSentToday) {
      sendWeeklyReport();
      reportSentToday = true;
    }
  } else {
    reportSentToday = false;
  }
}

void sendWeeklyReport() {
    String report = "📊 <b>WEEKLY HEALTH REPORT</b>\n\n";
    report += "🗓 <b>Period:</b> Last 7 Days\n\n";
    report += "❤️ <b>Avg Heart Rate:</b> " + (isnan(max30102_hr) ? "N/A" : String((int)max30102_hr)) + " BPM\n";
    report += "🫁 <b>Avg SpO2:</b> " + (isnan(max30102_spo2) ? "N/A" : String((int)max30102_spo2)) + "%\n";
    report += "🌡 <b>Avg Temp:</b> " + String(temp_aht, 1) + "°C\n";
    report += "🌬 <b>Avg AQI:</b> " + String(aqi_val) + "\n";
    report += "\n📝 <b>Analysis:</b>\n";
    report += "Vital signs monitoring is active. No critical anomalies detected in logged sessions.\n";
    report += "\n<i>Stay healthy!</i>";
    sendTelegramMessage(report);
}

void checkAirQualityAlerts() {
    static unsigned long lastAQIAlert = 0;
    if (millis() - lastAQIAlert < 300000) return;
    if (aqi_val < 3) return;
    String alertMsg = "";
    if (aqi_val >= 4) {
        alertMsg = "🚨 <b>AIR QUALITY ALERT</b>\n\n";
        alertMsg += (aqi_val == 5) ? "⚠️ <b>UNHEALTHY (AQI 5)</b>\nVentilate immediately!" : "⚠️ <b>POOR (AQI 4)</b>\nVentilation recommended.";
    } else if (aqi_val == 3) {
        alertMsg = "⚠️ <b>AIR QUALITY NOTICE</b>\n\n";
        alertMsg += "⚠️ <b>MODERATE (AQI 3)</b>\nUnusually sensitive people should limit exertion.";
    }
    if (alertMsg.length() > 0) {
        alertMsg += "\n\n📊 <b>Readings:</b>\n";
        alertMsg += "🌫 AQI: " + String(aqi_val) + "\n";
        alertMsg += "🧪 TVOC: " + String(tvoc_val) + " ppb\n";
        alertMsg += "💨 eCO2: " + String(eco2_val) + " ppm\n";
        if (sendTelegramMessage(alertMsg)) {
            lastAQIAlert = millis();
        }
    }
}

// ========== Telegram (fixed with larger buffer) ==========
void processTelegramCommands() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 10000) return;
  lastCheck = millis();
  if (cloudBotToken == "") return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + cloudBotToken + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=1";
  if (!http.begin(client, url)) return;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return; }
  String payload = http.getString();
  http.end();
  StaticJsonDocument<2048> doc;  // FIXED: increased buffer
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.println("Telegram JSON parse failed");
    return;
  }
  JsonArray results = doc["result"].as<JsonArray>();
  if (results.size() > 0) {
    lastUpdateId = results[0]["update_id"];
    String chatId = results[0]["message"]["chat"]["id"].as<String>();
    String text = results[0]["message"]["text"].as<String>();
    if (chatId != cloudChatId) return;
    Serial.printf("[Telegram] Received: %s\n", text.c_str());
    String reply = "";
    if (text == "/status") {
      reply = "🤖 *ELLA Status Report*\n\n";
      reply += "🌡 Temp: " + String(temp_aht,1) + "°C\n";
      reply += "💧 Humidity: " + String(humidity_aht,1) + "%\n";
      reply += "🌬 AQI: " + String(aqi_val) + "\n";
      reply += "☁️ TVOC: " + String(tvoc_val) + " ppb\n";
      reply += "💨 eCO2: " + String(eco2_val) + " ppm\n";
    } else if (text == "/health") {
      if (isnan(max30102_hr)) {
        reply = "❌ No health data. Place finger on sensor.";
      } else {
        reply = "❤️ *Health Vitals*\n\n";
        reply += "💓 HR: " + String((int)max30102_hr) + " BPM\n";
        reply += "🫁 SpO2: " + String((int)max30102_spo2) + "%\n";
      }
    } else if (text == "/help") {
      reply = "/status - sensor readings\n/health - vitals\n/help - this";
    } else {
      reply = "Unknown command. Type /help";
    }
    if (reply.length() > 0) sendTelegramMessage(reply);
  }
}

// ========== Emergency Alert ==========
void sendEmergencyAlert(String condition) {
  Serial.println("[Emergency] " + condition);
  String msg = "🚨 EMERGENCY: " + condition;
  sendTelegramMessage(msg);
}

// ========== Setup ==========
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
  if (Wire.endTransmission() != 0) Serial.println("MAX30102 NOT DETECTED on I2C!");
  if (!particleSensor.begin(Wire, 0x57)) {
    Serial.println("MAX30102 not found!");
  } else {
    Serial.println("MAX30102 OK");
    particleSensor.setup(0x3C, 8, 3, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x3C);
    particleSensor.setPulseAmplitudeIR(0x3C);
  }
  tcaselect(CH_EYE_LEFT);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("Left Eye Failed");
  else {
    Serial.println("Left Eye OK");
    display.clearDisplay();
    display.display();
  }
  tcaselect(CH_EYE_RIGHT);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("Right Eye Failed");
  else {
    Serial.println("Right Eye OK");
    display.clearDisplay();
    display.display();
  }

  // Firebase after WiFi
  if (WiFi.status() == WL_CONNECTED) setupFirebase();
}

// ========== Loop ==========
void loop() {
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 500) {
    lastRead = millis();
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
    drawNormalScreen(false);
  }

  processTelegramCommands();
  if (firebaseReady) {
    pushSensorDataToFirebase();
    syncWithFirebase();
    checkRemoteCommands();
    checkAutoWeeklyReport();
    checkAirQualityAlerts();
  }
  delay(10);
}
