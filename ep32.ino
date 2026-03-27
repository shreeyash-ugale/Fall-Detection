// ============================================================
//  ep32.ino  –  Fall Detection with Blynk Alert & Cancel Window
// ============================================================
//
//  Flow on fall detection:
//    1. Send push notification to Blynk mobile app.
//    2. Set Blynk virtual pin V1 = 1  (alert active).
//    3. Poll V1 for up to CANCEL_WINDOW_MS  (10 s).
//       • If user presses the Blynk button (V1 → 0) → cancel, no LED.
//       • If window expires with V1 still 1          → blink LED.
//
//  Blynk setup (mobile app):
//    • Button widget  → Virtual Pin V1  (SWITCH or PUSH mode)
//    • When user presses it the widget writes 0 to V1.
//
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// ─── WiFi ────────────────────────────────────────────────────
const char* WIFI_SSID     = "shree";
const char* WIFI_PASSWORD = "1234567890";

// ─── Backend server ──────────────────────────────────────────
const char* SERVER_URL = "http://13.50.226.242:8080/api/sensor";

// ─── Device identity ─────────────────────────────────────────
const String DEVICE_ID = "ESP32-NODE-1";

// ─── Blynk (HTTP API) ────────────────────────────────────────
//  Fill in your Blynk auth token and the correct server below.
const char* BLYNK_AUTH_TOKEN = "kJrph6uWEQMU0hrSyAh66ZulY0nvK6dp";   // ← replace
const char* BLYNK_SERVER     = "10.248.21.212:8080";             // or your server
// V1 – Cancel button: user presses this during the 10 s window to abort the alert
const int   BLYNK_CANCEL_PIN  = 1;
// V2 – Trigger (arm/disarm) toggle: device is ARMED only when this pin = 1
const int   BLYNK_TRIGGER_PIN = 2;

// ─── MPU6050 ─────────────────────────────────────────────────
const int MPU_ADDR = 0x68;

// ─── Hardware ────────────────────────────────────────────────
const int LED_PIN = 18;

// ─── ML batch ────────────────────────────────────────────────
const int BATCH_SIZE = 200;

// ─── Fall-alert timing ───────────────────────────────────────
const unsigned long CANCEL_WINDOW_MS   = 10000UL;  // 10 s user window
const unsigned long CANCEL_POLL_MS     =   500UL;  // poll Blynk every 500 ms
const float         FALL_THRESHOLD     =    0.99f;

// ─── Batch buffers ───────────────────────────────────────────
float accBatch [BATCH_SIZE][3];
float gyroBatch[BATCH_SIZE][3];
int   currentSample = 0;


// ============================================================
//  Forward declarations
// ============================================================
void initWiFi();
void initMPU6050();
void readMPUSample();
void sendData();
void handleFallDetected();
bool sendBlynkNotification(const String& message);
bool setBlynkVirtualPin(int pin, int value);
int  getBlynkVirtualPin(int pin);
bool waitForUserCancel();
void blinkLED();
void ensureWiFiConnected();


// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n=== ESP32 Fall Detector ===");

  initWiFi();
  initMPU6050();
}


// ============================================================
//  LOOP
// ============================================================
void loop() {
  ensureWiFiConnected();

  readMPUSample();
  currentSample++;

  if (currentSample >= BATCH_SIZE) {
    sendData();
    currentSample = 0;
  }

  delay(10);  // ~100 Hz sampling
}


// ============================================================
//  WiFi helpers
// ============================================================

/**
 * Block until WiFi is connected (called once at boot).
 */
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected – IP: ");
  Serial.println(WiFi.localIP());
}

/**
 * Reconnect WiFi if the connection was dropped (called every loop iteration).
 */
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi lost – reconnecting...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  delay(2000);
}


// ============================================================
//  MPU6050 helpers
// ============================================================

/**
 * Wake and configure the MPU6050 (±8 g accel, ±500°/s gyro).
 */
void initMPU6050() {
  // Wake up
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  // Accelerometer full-scale ±8 g
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x10);
  Wire.endTransmission(true);

  // Gyroscope full-scale ±500°/s
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x08);
  Wire.endTransmission(true);

  Serial.println("MPU6050 ready");
}

/**
 * Read one accelerometer + gyroscope sample into the batch buffers.
 * Returns silently if the sensor has no data yet.
 */
void readMPUSample() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);

  if (Wire.available() < 14) return;

  int16_t rawAcX = Wire.read() << 8 | Wire.read();
  int16_t rawAcY = Wire.read() << 8 | Wire.read();
  int16_t rawAcZ = Wire.read() << 8 | Wire.read();

  Wire.read(); Wire.read();  // skip temperature bytes

  int16_t rawGyX = Wire.read() << 8 | Wire.read();
  int16_t rawGyY = Wire.read() << 8 | Wire.read();
  int16_t rawGyZ = Wire.read() << 8 | Wire.read();

  // Convert to SI units
  accBatch [currentSample][0] = (rawAcX / 4096.0f) * 9.81f;
  accBatch [currentSample][1] = (rawAcY / 4096.0f) * 9.81f;
  accBatch [currentSample][2] = (rawAcZ / 4096.0f) * 9.81f;

  gyroBatch[currentSample][0] = rawGyX / 65.5f;
  gyroBatch[currentSample][1] = rawGyY / 65.5f;
  gyroBatch[currentSample][2] = rawGyZ / 65.5f;
}


// ============================================================
//  Send batch to ML server
// ============================================================

/**
 * Serialise the current batch as JSON, POST it to the backend,
 * and trigger handleFallDetected() if the model confirms a fall.
 */
void sendData() {
  Serial.println("--- Sending batch to server ---");

  WiFiClient client;
  HTTPClient http;

  http.begin(client, SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  // Build JSON payload
  StaticJsonDocument<8192> doc;
  doc["device_id"] = DEVICE_ID;

  JsonArray acc  = doc.createNestedArray("acc");
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

  Serial.print("Payload size: ");
  Serial.println(payload.length());

  int httpCode = http.POST(payload);

  Serial.print("HTTP code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Server response: " + response);

    DynamicJsonDocument resDoc(256);
    DeserializationError err = deserializeJson(resDoc, response);

    if (!err) {
      bool  fall       = resDoc["fall"]       | false;
      float confidence = resDoc["confidence"] | 0.0f;

      Serial.print("Fall: ");       Serial.println(fall);
      Serial.print("Confidence: "); Serial.println(confidence, 4);

      if (fall && confidence >= FALL_THRESHOLD) {
        Serial.println("⚠️  FALL CONFIRMED ⚠️");
        handleFallDetected();
      } else {
        Serial.println("No fall detected (below threshold).");
      }
    } else {
      Serial.print("JSON parse error: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.print("HTTP request failed: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}


// ============================================================
//  Fall response logic
// ============================================================

/**
 * Called when a confirmed fall is detected.
 *   0. Check if the device is ARMED (V2 = 1); return immediately if not.
 *   1. Notify user via Blynk push notification.
 *   2. Arm the virtual cancel button (V1 = 1).
 *   3. Give the user CANCEL_WINDOW_MS to press the button.
 *   4. If no cancellation → blink the LED.
 */
void handleFallDetected() {
  // ── Guard: only act when the Trigger toggle (V2) is ON ──────────────
  int armed = getBlynkVirtualPin(BLYNK_TRIGGER_PIN);
  if (armed != 1) {
    Serial.println("[ALERT] Device is UNARMED (V2 = 0) – fall alert suppressed.");
    return;
  }

  Serial.println("[ALERT] Device is ARMED. Fall detected – alerting user via Blynk...");

  // 1. Push notification to the mobile app
  sendBlynkNotification("⚠️ Fall detected! Press the button to cancel the alert.");

  // 2. Set V1 = 1  so the app knows alert is active
  setBlynkVirtualPin(BLYNK_CANCEL_PIN, 1);

  // 3. Wait for user to cancel
  bool cancelled = waitForUserCancel();

  if (cancelled) {
    Serial.println("[ALERT] User cancelled – no LED blink.");
  } else {
    Serial.println("[ALERT] No response within window – activating LED alarm!");
    blinkLED();
  }

  // 4. Disarm the virtual pin regardless
  setBlynkVirtualPin(BLYNK_CANCEL_PIN, 0);
}

/**
 * Poll the Blynk cancel pin (V1) for up to CANCEL_WINDOW_MS.
 * Returns true if the user cancelled (V1 became 0), false on timeout.
 */
bool waitForUserCancel() {
  Serial.print("[ALERT] Waiting ");
  Serial.print(CANCEL_WINDOW_MS / 1000);
  Serial.println(" s for user to cancel...");

  unsigned long start = millis();

  while (millis() - start < CANCEL_WINDOW_MS) {
    int pinValue = getBlynkVirtualPin(BLYNK_CANCEL_PIN);

    if (pinValue == 0) {
      Serial.println("[ALERT] Cancellation received.");
      return true;
    }

    // Brief blink to signal alert is active (non-blocking visual cue)
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);

    delay(CANCEL_POLL_MS - 100);  // rest of poll interval
  }

  return false;  // timeout – no cancel
}


// ============================================================
//  Blynk HTTP API helpers
// ============================================================

/**
 * Send a push notification through the Blynk HTTP API.
 * Returns true on HTTP 200, false otherwise.
 */
bool sendBlynkNotification(const String& message) {
  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + BLYNK_SERVER +
               "/external/api/notify?token=" + BLYNK_AUTH_TOKEN +
               "&body=" + message;

  Serial.println("[Blynk] Sending notification: " + message);

  http.begin(client, url);
  int code = http.GET();

  Serial.print("[Blynk] Notification HTTP code: ");
  Serial.println(code);

  http.end();
  return (code == 200);
}

/**
 * Write an integer value to a Blynk virtual pin via the HTTP API.
 * Returns true on HTTP 200, false otherwise.
 */
bool setBlynkVirtualPin(int pin, int value) {
  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + BLYNK_SERVER +
               "/external/api/update?token=" + BLYNK_AUTH_TOKEN +
               "&V" + String(pin) + "=" + String(value);

  Serial.print("[Blynk] SET V");
  Serial.print(pin);
  Serial.print(" = ");
  Serial.println(value);

  http.begin(client, url);
  int code = http.GET();

  Serial.print("[Blynk] SET HTTP code: ");
  Serial.println(code);

  http.end();
  return (code == 200);
}

/**
 * Read the current integer value of a Blynk virtual pin via the HTTP API.
 * Returns the pin value (0/1), or -1 on error.
 */
int getBlynkVirtualPin(int pin) {
  WiFiClient client;
  HTTPClient http;

  String url = String("http://") + BLYNK_SERVER +
               "/external/api/get?token=" + BLYNK_AUTH_TOKEN +
               "&V" + String(pin);

  http.begin(client, url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("[Blynk] GET V");
    Serial.print(pin);
    Serial.print(" failed, HTTP code: ");
    Serial.println(code);
    http.end();
    return -1;
  }

  String body = http.getString();
  http.end();

  // Blynk returns the raw value as a plain string, e.g. "1" or "0"
  body.trim();
  Serial.print("[Blynk] GET V");
  Serial.print(pin);
  Serial.print(" = ");
  Serial.println(body);

  return body.toInt();
}


// ============================================================
//  LED alarm
// ============================================================

/**
 * Blink the LED rapidly for 5 seconds to signal an unacknowledged fall.
 */
void blinkLED() {
  Serial.println("[LED] Starting alarm blink for 5 s...");

  unsigned long start = millis();

  while (millis() - start < 5000UL) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }

  digitalWrite(LED_PIN, LOW);
  Serial.println("[LED] Alarm blink done.");
}