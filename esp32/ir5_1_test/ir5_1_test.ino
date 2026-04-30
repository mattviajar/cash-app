/*
  ESP32 IR5-1 Sensor Test

  Wiring:
  - IR5-1 VCC -> 3.3V (or module-rated VCC)
  - IR5-1 GND -> GND
  - IR5-1 OUT -> GPIO32

  Notes:
  - Most 3-pin IR modules are active LOW:
      LOW  = object detected
      HIGH = clear
  - If opposite, set ACTIVE_LOW to false.
*/

constexpr int IR5_1_PIN = 32;
constexpr bool ACTIVE_LOW = true;
constexpr unsigned long PRINT_INTERVAL_MS = 120;
constexpr unsigned long HOLD_MS = 140;

unsigned long sampleIndex = 0;
unsigned long lastPrintMs = 0;
unsigned long detectStartedMs = 0;

void setup() {
  Serial.begin(115200);
  pinMode(IR5_1_PIN, INPUT);

  Serial.println("IR5-1 test ready");
  Serial.println("Pin: GPIO32");
  Serial.println("Bring target very close to sensor");
}

void loop() {
  const unsigned long nowMs = millis();
  const int raw = digitalRead(IR5_1_PIN);
  const bool detected = ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);

  if (detected) {
    if (detectStartedMs == 0) {
      detectStartedMs = nowMs;
    }
  } else {
    detectStartedMs = 0;
  }

  const bool stableDetected = (detectStartedMs != 0) && ((nowMs - detectStartedMs) >= HOLD_MS);

  if (nowMs - lastPrintMs >= PRINT_INTERVAL_MS) {
    lastPrintMs = nowMs;
    ++sampleIndex;

    Serial.print("sample=");
    Serial.print(sampleIndex);
    Serial.print(" raw=");
    Serial.print(raw);
    Serial.print(" detect=");
    Serial.print(detected ? "YES" : "NO");
    Serial.print(" stable=");
    Serial.print(stableDetected ? "YES" : "NO");
    Serial.print(" holdMs=");
    if (detectStartedMs == 0) {
      Serial.println(0);
    } else {
      Serial.println(nowMs - detectStartedMs);
    }
  }
}
