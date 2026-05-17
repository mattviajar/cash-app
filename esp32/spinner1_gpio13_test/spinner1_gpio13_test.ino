// Storage Spinner 1 quick test - direct ESP32 GPIO13 (no PCA9685).
// Spins forward 1 second, backward 1 second, then stops.
// Sends a SPIN command on USB serial to start, or set RUN_ON_BOOT=true to auto-run.
//
// Wiring:
//   Servo signal (orange/yellow) -> ESP32 GPIO13
//   Servo VCC    (red)           -> EXTERNAL 5V supply (NOT ESP32 5V pin)
//   Servo GND    (brown/black)   -> 5V supply GND  AND  ESP32 GND (common ground)

#include <ESP32Servo.h>

constexpr int   SERVO_PIN     = 13;
constexpr int   NEUTRAL_US    = 1500;   // stop
constexpr int   FORWARD_US    = 1300;   // forward (swapped: was 1700)
constexpr int   REVERSE_US    = 1700;   // backward (swapped: was 1300)
constexpr unsigned long FORWARD_MS  = 2000;
constexpr unsigned long BACKWARD_MS = 1000;
constexpr bool  RUN_ON_BOOT   = true;   // auto-spin once at boot

Servo spinner1;

void runCycle() {
  Serial.print("[spinner1] FORWARD ");
  Serial.print(FORWARD_MS);
  Serial.println("ms");
  spinner1.writeMicroseconds(FORWARD_US);
  delay(FORWARD_MS);

  Serial.println("[spinner1] STOP (brief)");
  spinner1.writeMicroseconds(NEUTRAL_US);
  delay(150);

  Serial.print("[spinner1] BACKWARD ");
  Serial.print(BACKWARD_MS);
  Serial.println("ms");
  spinner1.writeMicroseconds(REVERSE_US);
  delay(BACKWARD_MS);

  Serial.println("[spinner1] STOP");
  spinner1.writeMicroseconds(NEUTRAL_US);
  Serial.println("[spinner1] DONE");
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("=== Spinner 1 GPIO13 test ready ===");
  Serial.println("Send 'SPIN' over serial to repeat cycle.");

  spinner1.setPeriodHertz(50);
  spinner1.attach(SERVO_PIN, 500, 2500);
  spinner1.writeMicroseconds(NEUTRAL_US);
  delay(300);

  if (RUN_ON_BOOT) {
    runCycle();
  }
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "SPIN") {
      runCycle();
    } else if (cmd == "STOP") {
      spinner1.writeMicroseconds(NEUTRAL_US);
      Serial.println("[spinner1] STOP");
    } else if (cmd.length() > 0) {
      Serial.print("[spinner1] unknown cmd: ");
      Serial.println(cmd);
    }
  }
}
