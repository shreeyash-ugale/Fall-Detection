#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// ---------------- WIFI ----------------
const char* ssid = "shree";
const char* password = "1234567890";

// ---------------- SERVER ----------------
const char* serverUrl = "http://51.21.167.12:8080/api/sensor";

// ---------------- DEVICE ----------------
String DEVICE_ID = "ESP32-NODE-1";

// ---------------- MPU6050 ----------------
const int MPU_ADDR = 0x68;

// ---------------- LED ----------------  
const int LED_PIN = 18;

// ---------------- ML BATCH ----------------
const int BATCH_SIZE = 200;

// ---------------- STORAGE ----------------
float accBatch[BATCH_SIZE][3];
float gyroBatch[BATCH_SIZE][3];

int currentSample = 0;


// ------------------------------------------------
// LED BLINK FUNCTION
// ------------------------------------------------
void blinkLED() {

  unsigned long startTime = millis();

  while (millis() - startTime < 3000) {

    digitalWrite(LED_PIN, HIGH);
    delay(250);

    digitalWrite(LED_PIN, LOW);
    delay(250);
  }
}


// ------------------------------------------------
// SETUP
// ------------------------------------------------
void setup() {

  Serial.begin(115200);
  Wire.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("Starting ESP32...");

  // WiFi connect
  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  Serial.println(WiFi.localIP());

  // Wake MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  // Accelerometer ±8g
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x10);
  Wire.endTransmission(true);

  // Gyroscope ±500°/s
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x08);
  Wire.endTransmission(true);

  Serial.println("MPU6050 Ready");
}


// ------------------------------------------------
// LOOP
// ------------------------------------------------
void loop() {

  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi Lost. Reconnecting...");
    WiFi.begin(ssid, password);

    delay(2000);
    return;
  }

  // Request sensor data
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);

  if (Wire.available() < 14) return;

  // -------- ACCELEROMETER --------
  int16_t rawAcX = Wire.read() << 8 | Wire.read();
  int16_t rawAcY = Wire.read() << 8 | Wire.read();
  int16_t rawAcZ = Wire.read() << 8 | Wire.read();

  // Skip temperature
  Wire.read();
  Wire.read();

  // -------- GYROSCOPE --------
  int16_t rawGyX = Wire.read() << 8 | Wire.read();
  int16_t rawGyY = Wire.read() << 8 | Wire.read();
  int16_t rawGyZ = Wire.read() << 8 | Wire.read();


  // Convert accelerometer to m/s²
  accBatch[currentSample][0] = (rawAcX / 4096.0) * 9.81;
  accBatch[currentSample][1] = (rawAcY / 4096.0) * 9.81;
  accBatch[currentSample][2] = (rawAcZ / 4096.0) * 9.81;

  // Convert gyro to deg/s
  gyroBatch[currentSample][0] = rawGyX / 65.5;
  gyroBatch[currentSample][1] = rawGyY / 65.5;
  gyroBatch[currentSample][2] = rawGyZ / 65.5;

  currentSample++;

  // Send when batch ready
  if (currentSample >= BATCH_SIZE) {

    sendData();
    currentSample = 0;
  }

  // 100Hz sampling
  delay(10);
}


// ------------------------------------------------
// SEND DATA TO SERVER
// ------------------------------------------------
void sendData() {

  Serial.println("Preparing batch...");

  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<8192> doc;

  doc["device_id"] = DEVICE_ID;

  JsonArray acc = doc.createNestedArray("acc");
  JsonArray gyro = doc.createNestedArray("gyro");

  for (int i = 0; i < BATCH_SIZE; i++) {

    JsonArray a = acc.createNestedArray();
    a.add(accBatch[i][0]);
    a.add(accBatch[i][1]);
    a.add(accBatch[i][2]);

    JsonArray g = gyro.createNestedArray();
    g.add(gyroBatch[i][0]);
    g.add(gyroBatch[i][1]);
    g.add(gyroBatch[i][2]);
  }

  String payload;
  serializeJson(doc, payload);

  Serial.println("Sending data to server...");
  Serial.println("Payload size:");
  Serial.println(payload.length());

  int httpCode = http.POST(payload);

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {

    String response = http.getString();

    Serial.println("Server Response:");
    Serial.println(response);

    DynamicJsonDocument resDoc(256);

    DeserializationError error = deserializeJson(resDoc, response);

    if (!error) {

      bool fall = resDoc["fall"];
      float confidence = resDoc["confidence"] | 0.0;

      const float FALL_THRESHOLD = 0.99;

      Serial.print("Fall: ");
      Serial.println(fall);

      Serial.print("Confidence: ");
      Serial.println(confidence, 4);

      if (fall && confidence >= FALL_THRESHOLD) {

        Serial.println("⚠️ FALL DETECTED (CONFIRMED) ⚠️");
        blinkLED();

      } else {

        Serial.println("No fall detected (below threshold)");
      }

    }

  } else {

    Serial.println("HTTP request failed");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}