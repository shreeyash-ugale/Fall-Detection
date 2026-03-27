// ============================================================
//  ep32.ino  –  Fall Detection with Blynk Library Integration
// ============================================================
//
//  Blynk virtual pins:
//    V1 – Cancel button : user presses during 10 s window → no LED blink.
//    V2 – Trigger toggle: device is ARMED when ON (1), UNARMED when OFF (0).
//
//  Every loop iteration:
//    • Runs Blynk.run() so callbacks are processed.
//    • Checks isArmed flag (set by BLYNK_WRITE(V2) callback).
//    • If UNARMED → prints status, sleeps 1 s, skips all sensing.
//    • If ARMED   → samples MPU6050, batches, sends to ML server.
//
//  On confirmed fall (armed only):
//    1. Blynk.notify() push notification sent to mobile app.
//    2. cancelPressed flag cleared; LED gives visual warning.
//    3. Wait up to 10 s; if user presses V1 button → cancel.
//    4. No cancellation → blink LED full alarm.
//
//  Library install (Arduino IDE):
//    Library Manager → search "Blynk" → install "Blynk by Volodymyr Shymanskyy"
//
//  Local server connection:
//    Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD,
//                BLYNK_SERVER, BLYNK_PORT);
//
// ============================================================

// ─── Blynk config (must come BEFORE BlynkSimpleEsp32.h) ──────
#define BLYNK_PRINT Serial // pipe Blynk debug to Serial

#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h> // Blynk library for ESP32
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>


// ─── WiFi ────────────────────────────────────────────────────
const char *WIFI_SSID = "shree";
const char *WIFI_PASSWORD = "1234567890";

// ─── Blynk ───────────────────────────────────────────────────
const char *BLYNK_AUTH_TOKEN = "kJrph6uWEQMU0hrSyAh66ZulY0nvK6dp";
const char *BLYNK_SERVER = "10.248.21.212"; // local server IP
const int BLYNK_PORT = 8080;                // plain TCP port

// Virtual pin assignments
const int VP_CANCEL = V1;  // Cancel button widget in Blynk app
const int VP_TRIGGER = V2; // Trigger (arm/disarm) switch widget
const int VP_LCD = V3;     // LCD display widget

// ─── Backend ML server ───────────────────────────────────────
//const char *SERVER_URL = "http://13.50.226.242:8080/api/sensor";
const char *SERVER_URL = "http://10.248.21.212:8081/api/sensor";

// ─── Device identity ─────────────────────────────────────────
const String DEVICE_ID = "ESP32-NODE-1";

// ─── MPU6050 ─────────────────────────────────────────────────
const int MPU_ADDR = 0x68;

// ─── Hardware ────────────────────────────────────────────────
const int LED_PIN = 18;  // D18 – steady-on alarm LED
const int LED_PIN2 = 19; // D19 – blink-while-waiting LED

// ─── ML batch / sliding window ──────────────────────────────
const int BATCH_SIZE = 200;

// ─── Fall-alert timing ───────────────────────────────────────
const unsigned long CANCEL_WINDOW_MS = 10000UL; // 10 s cancel window
const float FALL_THRESHOLD = 0.99f;

// ─── Ring buffer (circular) ──────────────────────────────────
// head always points to the NEXT slot to write.
// When head wraps around it naturally overwrites the oldest sample,
// so no readings are ever skipped regardless of delays elsewhere.
float accBatch[BATCH_SIZE][3];
float gyroBatch[BATCH_SIZE][3];
volatile int ringHead = 0; // index of next write slot (0 … BATCH_SIZE-1)
volatile bool ringFull =
    false; // true once the buffer has been filled at least once
volatile int stepCount = 0; // new samples since last sendData() call

// STEP_SIZE: how many new samples must arrive before the window is
// re-evaluated. Smaller  → more frequent checks, shorter miss window around
// HTTP delays. Larger   → fewer HTTP calls, less server load. At ~100 Hz,
// STEP_SIZE=50 → evaluate every ~0.5 s.
const int STEP_SIZE = 50;

// ─── State flags (written by Blynk callbacks) ────────────────
volatile bool isArmed = false;       // true when V2 toggle is ON
volatile bool cancelPressed = false; // true when V1 button pressed

// ─── Blynk LCD widget ────────────────────────────────────────
WidgetLCD lcd(VP_LCD);

// ============================================================
//  BLYNK CALLBACKS  (called automatically by Blynk.run())
// ============================================================

/**
 * Called whenever the Blynk app writes to V2 (Trigger toggle).
 * Updates the global isArmed flag immediately.
 */
BLYNK_WRITE(V2) {
  isArmed = (param.asInt() == 1);
  Serial.print("[Blynk] Trigger (V2) → ");
  Serial.println(isArmed ? "ARMED" : "UNARMED");
}

/**
 * Called whenever the Blynk app writes to V1 (Cancel button).
 * Sets cancelPressed when button is pressed (value = 1).
 */
BLYNK_WRITE(V1) {
  if (param.asInt() == 1) {
    cancelPressed = true;
    Serial.println("[Blynk] Cancel button pressed (V1).");
  }
}

/**
 * Called once when Blynk connects (or reconnects).
 * Syncs V2 so isArmed reflects the current toggle state
 * without the user having to toggle it again after boot.
 */
BLYNK_CONNECTED() {
  Serial.println("[Blynk] Connected to server.");
  Blynk.syncVirtual(VP_TRIGGER); // fires BLYNK_WRITE(V2) immediately
}

// ============================================================
//  Forward declarations
// ============================================================
void initMPU6050();
void readMPUSample();
void sendData();
void handleFallDetected();
bool waitForUserCancel();
void blinkLED();
void sensorTaskCode(void *pvParameters);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(LED_PIN2, OUTPUT);
  digitalWrite(LED_PIN2, LOW);

  Serial.println("\n=== ESP32 Fall Detector ===");

  // Blynk.begin() handles WiFi connection AND Blynk server connection
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD, BLYNK_SERVER,
              BLYNK_PORT);

  initMPU6050();

  // Create dedicated FreeRTOS task for sensor sampling
  xTaskCreatePinnedToCore(sensorTaskCode, /* Task function */
                          "SensorTask",   /* Name of task */
                          10000,          /* Stack size */
                          NULL,           /* Parameter */
                          1,    /* Priority (1 is fine, higher than idle) */
                          NULL, /* Task handle */
                          0);   /* Pin to core 0 (loop runs on core 1) */
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Process Blynk callbacks (keeps connection alive, fires BLYNK_WRITE)
  Blynk.run();

  // ── Trigger (arm/disarm) gate ─────────────────────────────────────────
  // isArmed is updated instantly via BLYNK_WRITE(V2) callback above.
  if (!isArmed) {
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 1000) {
      Serial.println("[STATUS] Device is UNARMED – monitoring paused.");
      lastPrint = millis();
    }
    delay(10);
    return;
  }

  // Sliding window: evaluate the full BATCH_SIZE window every STEP_SIZE new
  // samples. This means a fall is caught within ~0.5 s of occurring, even if
  // the previous HTTP round-trip took a long time.
  // Note: Data is collected concurrently by sensorTaskCode on Core 0!
  if (ringFull && stepCount >= STEP_SIZE) {
    stepCount = 0;
    sendData();
  }

  delay(10);
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
 * Read one accelerometer + gyroscope sample into the ring buffer slot
 * given by ringHead.  The CALLER is responsible for advancing ringHead
 * after this returns, so this function is safe to call from any context.
 * Skips silently if the sensor has no data ready.
 */
void readMPUSample() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);

  if (Wire.available() < 14)
    return;

  int16_t rawAcX = Wire.read() << 8 | Wire.read();
  int16_t rawAcY = Wire.read() << 8 | Wire.read();
  int16_t rawAcZ = Wire.read() << 8 | Wire.read();

  Wire.read();
  Wire.read(); // skip temperature bytes

  int16_t rawGyX = Wire.read() << 8 | Wire.read();
  int16_t rawGyY = Wire.read() << 8 | Wire.read();
  int16_t rawGyZ = Wire.read() << 8 | Wire.read();

  // Convert to SI units – write into current ringHead slot
  accBatch[ringHead][0] = (rawAcX / 4096.0f) * 9.81f;
  accBatch[ringHead][1] = (rawAcY / 4096.0f) * 9.81f;
  accBatch[ringHead][2] = (rawAcZ / 4096.0f) * 9.81f;

  gyroBatch[ringHead][0] = rawGyX / 65.5f;
  gyroBatch[ringHead][1] = rawGyY / 65.5f;
  gyroBatch[ringHead][2] = rawGyZ / 65.5f;
}

// ============================================================
//  Sensor Task (runs on Core 0)
// ============================================================

/**
 * FreeRTOS task to continuously sample the MPU6050 at ~100 Hz.
 * Because this runs independently, blocking HTTP requests on Core 1
 * won't miss any sensor data.
 */
void sensorTaskCode(void *pvParameters) {
  for (;;) {
    if (isArmed) {
      readMPUSample();
      ringHead = (ringHead + 1) % BATCH_SIZE;
      if (ringHead == 0)
        ringFull = true;
      stepCount++;
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // exactly 10 ms delay for ~100Hz
  }
}

// ============================================================
//  Send batch to ML server
// ============================================================

/**
 * Serialise the current batch as JSON, POST it to the backend,
 * then call handleFallDetected() if the model confirms a fall.
 */
void sendData() {
  Serial.println("--- Sending batch to server ---");

  // ── Linearise the ring buffer in chronological order ──────────────────
  // ringHead points to the NEXT write slot, so the oldest sample is at
  // ringHead itself (when the buffer is full).
  // We copy into a temporary linear array so the JSON loop is simple.
  float linAcc[BATCH_SIZE][3];
  float linGyro[BATCH_SIZE][3];
  for (int i = 0; i < BATCH_SIZE; i++) {
    int src = (ringHead + i) % BATCH_SIZE; // oldest → newest
    linAcc[i][0] = accBatch[src][0];
    linAcc[i][1] = accBatch[src][1];
    linAcc[i][2] = accBatch[src][2];
    linGyro[i][0] = gyroBatch[src][0];
    linGyro[i][1] = gyroBatch[src][1];
    linGyro[i][2] = gyroBatch[src][2];
  }

  // ── While HTTP is busy, keep sampling so no readings are lost ─────────
  // We start the HTTP request on a background-style approach:
  // build payload first, then POST – reading continues during network wait.

  WiFiClient client;
  HTTPClient http;

  http.begin(client, SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  // Build JSON payload from the linearised snapshot
  StaticJsonDocument<8192> doc;
  doc["device_id"] = DEVICE_ID;

  JsonArray acc = doc.createNestedArray("acc");
  JsonArray gyro = doc.createNestedArray("gyro");

  for (int i = 0; i < BATCH_SIZE; i++) {
    JsonArray a = acc.createNestedArray();
    a.add(linAcc[i][0]);
    a.add(linAcc[i][1]);
    a.add(linAcc[i][2]);

    JsonArray g = gyro.createNestedArray();
    g.add(linGyro[i][0]);
    g.add(linGyro[i][1]);
    g.add(linGyro[i][2]);
  }

  String payload;
  serializeJson(doc, payload);

  Serial.print("Payload size: ");
  Serial.println(payload.length());

  // POST – the HTTPClient is synchronous so it blocks during the network
  // round-trip. The sensorTask is running concurrently on Core 0,
  // so sampling continues uninterrupted!
  int httpCode = http.POST(payload);

  stepCount =
      0; // reset step so next evaluation starts from a clean count after HTTP

  Serial.print("HTTP code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Server response: " + response);

    DynamicJsonDocument resDoc(256);
    DeserializationError err = deserializeJson(resDoc, response);

    if (!err) {
      bool fall = resDoc["fall"] | false;
      float confidence = resDoc["confidence"] | 0.0f;

      Serial.print("Fall: ");
      Serial.println(fall);
      Serial.print("Confidence: ");
      Serial.println(confidence, 4);

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
 * Orchestrates the full fall alert sequence.
 * Called only when isArmed == true (guaranteed by loop() gate).
 *
 *   1. Push notification via Blynk.notify().
 *   2. Clear cancelPressed flag.
 *   3. Poll for up to CANCEL_WINDOW_MS for user to press V1.
 *   4. No cancel → blink LED alarm.
 */
void handleFallDetected() {
  Serial.println("[ALERT] Fall detected – displaying on Blynk LCD...");

  // 1. Display on Blynk LCD
  lcd.clear();
  lcd.print(0, 0, "Fall Detected!");
  lcd.print(0, 1, "Press Cancel");

  // 2. Reset the cancel flag so a stale press doesn't immediately cancel
  cancelPressed = false;

  // 3. Wait for user cancel or timeout
  bool cancelled = waitForUserCancel();

  if (cancelled) {
    Serial.println("[ALERT] User cancelled – no LED alarm.");
  } else {
    Serial.println("[ALERT] No response – activating LED alarm!");
    blinkLED();
  }
}

/**
 * Waits up to CANCEL_WINDOW_MS for the user to press the V1 button.
 * While waiting:
 *   • Calls Blynk.run() continuously so the BLYNK_WRITE(V1) callback fires.
 *   • Does a brief LED blink each cycle as a visual active-alert cue.
 *
 * Returns true if cancelled, false on timeout.
 */
bool waitForUserCancel() {
  Serial.print("[ALERT] Waiting ");
  Serial.print(CANCEL_WINDOW_MS / 1000);
  Serial.println(" s for user cancel (V1)...");

  unsigned long start = millis();
  bool blinkOn = false;

  while (millis() - start < CANCEL_WINDOW_MS) {
    Blynk.run(); // essential: processes incoming V1 press from the app

    if (cancelPressed) {
      Serial.println("[ALERT] Cancellation received via V1.");
      digitalWrite(LED_PIN2, LOW);
      lcd.clear();
      return true;
    }

    // Brief visual cue: blink D19 while waiting (100ms on, 400ms off)
    unsigned long cycleTime = (millis() - start) % 500;
    if (cycleTime < 100) {
      if (!blinkOn) {
        digitalWrite(LED_PIN2, HIGH);
        blinkOn = true;
      }
    } else {
      if (blinkOn) {
        digitalWrite(LED_PIN2, LOW);
        blinkOn = false;
      }
    }

    delay(10);
  }

  // Ensure D19 is off when the window closes
  digitalWrite(LED_PIN2, LOW);
  return false; // timeout
}

// ============================================================
//  LED alarm
// ============================================================

/**
 * Keeps D18 LED on for 3 seconds to signal an unacknowledged fall.
 * LCD displays the alarm message for the full 3 seconds.
 */
void blinkLED() {
  Serial.println("[LED] D18 alarm on (3 s)...");

  // Display LED and LCD for 3 seconds
  digitalWrite(LED_PIN, HIGH);
  lcd.print(0, 0, "ALARM!");
  lcd.print(0, 1, "Fall Alert!");
  delay(3000);

  // Ensure LED is off and LCD is cleared
  digitalWrite(LED_PIN, LOW);
  lcd.clear();

  Serial.println("[LED] D18 alarm off.");
}