/*
  Uno IR1 Sensor Test (VCC, GND, OUT)

  Wiring for this test:
  - Sensor VCC -> 5V
  - Sensor GND -> GND
  - Sensor OUT -> A0 (IR1)

  Notes:
  - Most IR obstacle sensors are active LOW:
      LOW  = object detected
      HIGH = no object
  - If your sensor behaves opposite, set ACTIVE_LOW to false.
*/

const int IR_OUT_PIN = A0;
const bool ACTIVE_LOW = true;
const unsigned long PRINT_INTERVAL_MS = 120;
const unsigned long HOLD_MS = 120;

unsigned long sampleIndex = 0;
unsigned long lastPrintMs = 0;
unsigned long detectStartedMs = 0;

void setup() {
  Serial.begin(115200);
  pinMode(IR_OUT_PIN, INPUT);

  Serial.println("IR1 test ready");
  Serial.println("OUT pin on A0 (Uno IR1)");
  Serial.println("Bring object very close to test trigger");
}

void loop() {
  unsigned long nowMs = millis();
  int raw = digitalRead(IR_OUT_PIN);
  bool detected = ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);

  if (detected) {
    if (detectStartedMs == 0) {
      detectStartedMs = nowMs;
    }
  } else {
    detectStartedMs = 0;
  }

  bool stableDetected = (detectStartedMs != 0) && ((nowMs - detectStartedMs) >= HOLD_MS);

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
