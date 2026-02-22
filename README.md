# EllaBox – A remote health assistant

EllaBox is an all‑in‑one smart health assistant built on the **ESP32‑S3**. It combines voice interaction, real‑time environmental and medical sensors, expressive OLED eyes, a touchscreen UI, and cloud connectivity (Firebase / Telegram) into a single compact device.

![EllaBox UI Preview](docs/ui_preview.jpg) *– actual photo to be added*

---

## ✨ Features

- **Voice‑first AI Assistant**  
  Always listening (Deepgram Nova‑3), with Groq Llama 3.3 70B for intelligent, caring responses.  
  Supports web search (Serper.dev) and music playback (internet radio).

- **Medical Monitoring**  
  Heart rate (BPM) and blood oxygen (SpO₂) via MAX30102.  
  Ambient temperature / humidity (AHT20) and air quality (ENS160 – AQI, TVOC, eCO₂).  
  Body temperature estimation from the MAX30102.

- **Expressive OLED Eyes**  
  Two 128×64 OLED displays (via I²C multiplexer) show emotions: 😊😢🤔❤️😜😵 etc.  
  Eyes blink naturally and react to AI mood tags.

- **Touchscreen UI**  
  2.4" ILI9341 TFT with XPT2046 touch.  
  Normal mode shows sensor cards; AI mode shows live speech transcription and a pulsing animation.

- **Tactile Button & Ultrasonic Wake**  
  Short press toggles modes / stops audio. Long press sends an emergency Telegram alert.  
  Wave your hand in front (< 50 cm) to wake the assistant.

- **Cloud Sync & Alerts**  
  Sensor data pushed to **Firebase Realtime Database** in real time.  
  Receive **Telegram alerts** for abnormal vitals, poor air quality, or emergency button presses.  
  Reminders synced from the cloud are spoken at the correct time.

- **Weekly Reports & Remote Configuration**  
  Automatic Telegram reports every Sunday.  
  Wi‑Fi credentials and user profile (name, emergency contact) can be updated remotely via Firebase.

---

## 🧰 Hardware Requirements

| Component              | Model / Specification               | Pin / Notes                                      |
|------------------------|-------------------------------------|--------------------------------------------------|
| **Microcontroller**    | ESP32‑S3 (with PSRAM)               | `ESP32S3_DEV`                                    |
| **TFT Display**        | ILI9341 2.4" 240×320                | CS=10, DC=2, MOSI=11, SCK=12, MISO=13           |
| **Touch Controller**   | XPT2046                             | CS=47, IRQ=14 (shares SPI)                       |
| **OLED Eyes**          | 2× SSD1306 128×64 (I²C)             | via TCA9548A (CH0=left, CH1=right)               |
| **Microphone**         | I²S digital mic (e.g. INMP441)      | SCK=15, WS=7, SD=4                               |
| **Speaker**            | I²S DAC + amplifier (e.g. MAX98357) | BCLK=48, LRC=21, DOUT=18                         |
| **MAX30102**           | Heart rate / SpO₂ sensor             | I²C channel 4 (TCA9548A CH4)                     |
| **AHT20**              | Temp + humidity sensor               | I²C channel 2 (TCA9548A CH2)                     |
| **ENS160**             | Air quality sensor (AQI, TVOC, eCO₂)| I²C channel 2 (address 0x53)                     |
| **Ultrasonic**         | HC‑SR04                              | TRIG=5, ECHO=16                                   |
| **Tactile Button**     | Momentary switch                     | GPIO38 (internal pull‑up)                         |
| **Buzzer**             | Passive buzzer                       | GPIO40                                            |
| **I²C Multiplexer**    | TCA9548A                             | SDA=8, SCL=9                                      |
| **Power**              | 5V USB‑C or Li‑Po battery            |                                                   |

> **Note:** All pin assignments are defined at the top of the sketch. You can change them to match your wiring.

---

## 🔧 Software Setup

### 1. Required Libraries

Install the following libraries via the Arduino Library Manager (or manually):

- `WebSockets` by Markus Sattler
- `ArduinoJson` by Benoit Blanchon (v6)
- `ESP_I2S` (ESP32 Arduino core)
- `Adafruit GFX` & `Adafruit ILI9341`
- `XPT2046_Touchscreen`
- `U8g2` or `Adafruit SSD1306` (for OLED)
- `Adafruit AHTX0`
- `ScioSense_ENS16x`
- `MAX30105` by SparkFun
- `Firebase ESP Client` by mobizt
- `Preferences` (built‑in)

Make sure your **ESP32 Arduino core** is version **3.0.0 or newer**.

### 2. Clone / Download the Code

```bash
git clone https://github.com/yourusername/EllaBox.git
```

Open `EllaBox.ino` in the Arduino IDE.

### 3. Configure Credentials

Edit the top of the sketch to set your Wi‑Fi and API keys:

```cpp
const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";

const char* DEEPGRAM_KEY = "your_deepgram_api_key";
const char* GROQ_KEY = "your_groq_api_key";
const char* SERPER_KEY = "your_serper_api_key";   // optional, for web search

// Firebase
const char* FIREBASE_HOST = "your_project.firebaseio.com";
const char* FIREBASE_DATABASE_URL = "https://your_project.firebaseio.com";
const char* FIREBASE_AUTH = "your_web_api_key";
const char* FIREBASE_DB_SECRET = "your_database_secret";
```

**Firebase Setup:**

- Create a Realtime Database in **locked mode**.
- Copy the **Database URL** and **Database Secret** (found in Project Settings → Service accounts).
- Enable Email/Password or Anonymous sign‑in (the sketch uses the secret for legacy auth).

**Telegram Bot:**

- Talk to [@BotFather](https://t.me/botfather) to create a bot and get a token.
- Start a chat with your bot and send a dummy message.
- Retrieve your **chat ID** (e.g., by calling `getUpdates` or using a service like @getidsbot).
- The bot token and chat ID will be automatically synced from Firebase after you set them in the database (see below).

### 4. Firebase Data Structure

The sketch expects the following paths in your Realtime Database:

```
/commands
  /userProfile
    - name: "John Doe"
    - telegramBotToken: "123456:ABC..."
    - telegramChatId: "123456789"
    - emergencyContact: "+1234567890"
  /wifiConfig
    - ssid: "newSSID"
    - password: "newPass"
  /emergency: false   (set to true to trigger alert)

/reminders
  - id1: { detail: "Take medicine", time: "14:30", type: "pill" }
  - id2: { detail: "Water plants", time: "09:00", type: "chore" }

/readings          (automatically populated by EllaBox)
  - timestamp, temperature, humidity, heartRate, spo2, aqi, tvoc, eco2, bodyTemp
```

The sketch syncs user profile and reminders on boot and periodically.

### 5. Upload to ESP32‑S3

Select the correct board (e.g. `ESP32S3 Dev Module`) and port, then upload.

After upload, the device will:

- Show a startup animation and attempt to connect to Wi‑Fi.
- If Wi‑Fi fails, it continues in **offline mode** (most features disabled).
- Once connected, it syncs time via NTP and connects to Firebase, Deepgram, etc.

---

## 🎮 Usage Guide

### Modes

- **NORMAL mode** (default) – Shows sensor cards. Tap the screen or short‑press the button to switch to AI mode.
- **AI mode** – Microphone is active. Speak after the listening tone. The assistant’s response is spoken and displayed on screen. Tap the bottom‑left **BACK** button or short‑press the button to return to normal mode.

### Button Actions

- **Short press (< 500 ms)**  
  - If in AI mode and audio is playing → stops audio, resets mic.  
  - Otherwise toggles between NORMAL and AI mode.

- **Long press (> 2 s)**  
  Sends an **emergency Telegram alert** with the user’s name and contact info (if configured). A loud alarm sounds.

### Touchscreen

- **Normal mode** – Tapping anywhere (except the bottom nav bar) switches to AI mode.
- **AI mode** – Tapping the **BACK** button (bottom left) returns to normal mode.

### Medical Checkup (Normal Mode)

1. Place your finger on the MAX30102 sensor.
2. The screen will guide you through:
   - Place finger (5 s countdown)
   - Keep still (5 s)
   - Measuring heart rate (10 s), SpO₂ (10 s), temperature (10 s)
3. Results are displayed and spoken. Abnormal values trigger a Telegram alert.

### Voice Commands

After the listening tone, you can ask:

- *“What’s the temperature?”* – replies with ambient sensor data.
- *“Play some jazz.”* – streams an internet radio station (genre keywords: lofi, jazz, pop, rock, etc.).
- *“Search for latest technology news.”* – performs a web search (if Serper key is set).
- *“How are you feeling?”* – engages in casual conversation.

The AI will also respond to reminders and health queries using the latest sensor readings.

---

## 📱 Telegram Integration

Once the bot token and chat ID are synced from Firebase, you can send commands to your bot:

- `/status` – Current sensor readings
- `/health` – Latest heart rate, SpO₂, body temp
- `/weekly_report` – Summary of the past week (placeholder)
- `/help` – List of commands

You will also receive automatic alerts for:

- **Emergency button** press
- **Abnormal vitals** (low SpO₂, high/low heart rate)
- **Poor air quality** (AQI ≥ 4)
- **Reminders** at the scheduled time

---

## 🔄 Remote Configuration

If you need to change Wi‑Fi credentials without re‑uploading:

1. Write a JSON object to `/commands/wifiConfig` in Firebase:
   ```json
   { "ssid": "newSSID", "password": "newPass" }
   ```
2. EllaBox will read it, save to Preferences, and restart automatically.

Similarly, the user profile (name, emergency contact, Telegram credentials) can be updated under `/commands/userProfile`.

---

## 🛠 Troubleshooting

- **“No WiFi”** – Check SSID/password. The device tries saved credentials first, then hardcoded fallback.
- **Deepgram not connecting** – Ensure your API key is correct and you have sufficient credits. The sketch disconnects Deepgram during Telegram calls to free SSL resources; it will auto‑reconnect.
- **MAX30102 no readings** – Verify wiring and I²C address (0x57). Try lowering the sample rate in `particleSensor.setup()` if you see instability.
- **Audio hiss/noise** – Check I²S wiring, especially grounding. The microphone gain is set to 18× – you can adjust `GAIN_BOOSTER_I2S`.
- **Out of memory** – The sketch is optimised for PSRAM. If your board lacks PSRAM, reduce buffer sizes or disable some features.

---

## 📜 License

This project is open source under the **MIT License**. Feel free to modify and distribute.

---

## 🙌 Credits

- Deepgram for lightning‑fast STT
- Groq for ultra‑fast LLM inference
- Serper.dev for Google Search API
- Firebase for real‑time cloud sync
- All the amazing library authors

---

**Enjoy your personal health companion!**  
For issues or suggestions, please open a GitHub issue.
