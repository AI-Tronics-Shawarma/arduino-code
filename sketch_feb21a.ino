#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <MPU6050.h>
#include <U8g2lib.h>
#include "MAX30105.h"

// ---------------- WiFi ----------------
#define WIFI_SSID "NothingPhone1"
#define WIFI_PASS "qwertyuiop"
#define SERVER_URL "http://10.68.135.149:5000/data"

// ---------------- OLED ----------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0);

// ---------------- Sensors -------------
MAX30105 sensor;
BH1750 lightMeter;
MPU6050 mpu;

// ---------------- Pins ----------------
#define LM35_PIN 34
#define BUZZER_PIN 25

// ---------------- Timers --------------
unsigned long lastDisplayTime = 0;
unsigned long lastSerialTime  = 0;
unsigned long lastUploadTime  = 0;
unsigned long tiltStartTime   = 0;

// ---------------- Values --------------
float bpm_display   = 75;
float rmssd_display = 40;
float lux_display   = 0;
float temp_display  = 0;

bool motion_display = false;   // logic value (always 0 here)
bool tiltActive     = false;
bool buzzerOn       = false;

int fingerThreshold = 30000;

// ---------------- Temperature (LM35) ----------------
float getBodyTemp() {
  int adc = analogRead(LM35_PIN);
  float voltage = adc * (3.3 / 4095.0);
  return voltage * 100.0;
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);   // ESP32 I2C pins

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // OLED
  oled.begin();
  oled.setFont(u8g2_font_6x12_tf);
  oled.clearBuffer();
  oled.drawStr(0, 20, "Connecting WiFi...");
  oled.sendBuffer();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  // MAX30102
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    oled.clearBuffer();
    oled.drawStr(0, 20, "MAX30102 ERROR!");
    oled.sendBuffer();
    while (1);
  }
  sensor.setup();
  sensor.setPulseAmplitudeIR(0xFF);

  // BH1750
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // MPU6050
  mpu.initialize();
}

// ---------------- LOOP ----------------
void loop() {

  // -------- Finger Detection --------
  long ir = sensor.getIR();
  bool fingerOn = ir > fingerThreshold;

  // -------- Light --------
  lux_display = lightMeter.readLightLevel();

  // -------- Motion & Buzzer Logic (UPDATED) --------
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);

  float angle = atan2(ax, az) * 180.0 / PI;
  if (angle < 0) angle += 180;

  motion_display = false;   // logic remains 0
  buzzerOn = false;

  // ---- 180 degree for 5 seconds ----
  if (angle > 160 && angle <= 180) {

    if (!tiltActive) {
      tiltActive = true;
      tiltStartTime = millis();
    }

    if (millis() - tiltStartTime >= 5000) {
      buzzerOn = true;
    }
  }
  // ---- 90 degree (NO buzzer, logic 0) ----
  else if (angle > 80 && angle < 100) {
    tiltActive = false;
  }
  // ---- Any other angle ----
  else {
    tiltActive = false;
  }

  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);

  // -------- Temperature --------
  temp_display = getBodyTemp();

  // -------- HR + RMSSD (Simulated) --------
  if (fingerOn) {
    bpm_display += (random(-3, 4) * 0.2);
    bpm_display = constrain(bpm_display, 70, 87);

    rmssd_display = 50 - ((bpm_display - 70) * 1.2);
    rmssd_display = constrain(rmssd_display, 20, 55);
  }

  // -------- OLED Update --------
  if (millis() - lastDisplayTime > 1500) {
    lastDisplayTime = millis();
    oled.clearBuffer();

    if (!fingerOn) {
      oled.drawStr(0, 10, "NO FINGER");
    } else {
      char b1[32];
      sprintf(b1, "HR: %.1f BPM", bpm_display);
      oled.drawStr(0, 10, b1);

      char b2[32];
      sprintf(b2, "RMSSD: %.1f", rmssd_display);
      oled.drawStr(0, 22, b2);
    }

    char b3[32];
    sprintf(b3, "Lux: %.1f", lux_display);
    oled.drawStr(0, 36, b3);

    char b4[32];
    sprintf(b4, "Temp: %.1f C", temp_display);
    oled.drawStr(0, 48, b4);

    oled.drawStr(0, 58, buzzerOn ? "BUZZER: ON" : "BUZZER: OFF");

    oled.sendBuffer();
  }

  // -------- Serial Debug --------
  if (millis() - lastSerialTime > 500) {
    lastSerialTime = millis();
    Serial.print("Angle: ");
    Serial.print(angle);
    Serial.print(" | Buzzer: ");
    Serial.println(buzzerOn ? "ON" : "OFF");
  }

  // -------- Send JSON --------
  if (millis() - lastUploadTime > 1000) {
    lastUploadTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      http.begin(client, SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      String jsonData;
      jsonData.reserve(128);
      jsonData = "{";
      jsonData += "\"heartRate\":" + String(fingerOn ? bpm_display : 0) + ",";
      jsonData += "\"rmssd\":" + String(fingerOn ? rmssd_display : 0) + ",";
      jsonData += "\"lux\":" + String(lux_display) + ",";
      jsonData += "\"tempC\":" + String(temp_display) + ",";
      jsonData += "\"motion\":0,";
      jsonData += "\"buzzer\":" + String(buzzerOn ? 1 : 0);
      jsonData += "}";

      http.POST(jsonData);
      http.end();
    }
  }
}