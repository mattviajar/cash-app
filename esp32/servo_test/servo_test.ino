#if defined(ESP32)
#include <ESP32Servo.h>
#else
#include <Servo.h>
#endif

Servo testServo;

const int SERVO_PIN = 13;       // Change to the GPIO pin you are using.
const int BUTTON_FORWARD_PIN = 12; // Forward button to GND. Uses internal pull-up.
const int BUTTON_BACKWARD_PIN = 11; // Backward button to GND. Uses internal pull-up.
// Continuous rotation servo control (typical values):
// 1500us = stop, >1500us = one direction, <1500us = opposite direction.
const int NEUTRAL_US = 1500;    // Tune +/- 5..20 if servo drifts when "stopped".
const int FORWARD_US = 1700;    // Increase difference from NEUTRAL_US for more speed.
const int REVERSE_US = 1300;

const unsigned long DEBOUNCE_MS = 25;

bool isMoving = false;
int moveDirection = 0;           // 1 = forward, -1 = backward, 0 = stopped
bool stableForwardPressed = false;
bool rawForwardPressed = false;
bool stableBackwardPressed = false;
bool rawBackwardPressed = false;
unsigned long lastForwardDebounceMs = 0;
unsigned long lastBackwardDebounceMs = 0;
unsigned long moveStartMs = 0;
unsigned long totalForwardTimeMs = 0;
unsigned long totalBackwardTimeMs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  testServo.attach(SERVO_PIN);
  testServo.writeMicroseconds(NEUTRAL_US);
  pinMode(BUTTON_FORWARD_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BACKWARD_PIN, INPUT_PULLUP);

  Serial.println("Continuous servo button timing test started.");
  Serial.println("Forward button (GPIO 12): spins forward.");
  Serial.println("Backward button (GPIO 11): spins backward.");
}

void loop() {
  // Active-low buttons with pull-up: LOW means pressed.
  bool currentRawForward = (digitalRead(BUTTON_FORWARD_PIN) == LOW);
  bool currentRawBackward = (digitalRead(BUTTON_BACKWARD_PIN) == LOW);

  // Debounce forward button.
  if (currentRawForward != rawForwardPressed) {
    rawForwardPressed = currentRawForward;
    lastForwardDebounceMs = millis();
  }

  if ((millis() - lastForwardDebounceMs) >= DEBOUNCE_MS && stableForwardPressed != rawForwardPressed) {
    stableForwardPressed = rawForwardPressed;

    if (stableForwardPressed && !isMoving) {
      isMoving = true;
      moveDirection = 1;
      moveStartMs = millis();
      testServo.writeMicroseconds(FORWARD_US);
      Serial.print("[FORWARD] START ms=");
      Serial.println(moveStartMs);
    } else if (!stableForwardPressed && moveDirection == 1) {
      unsigned long stopMs = millis();
      unsigned long runMs = stopMs - moveStartMs;
      totalForwardTimeMs += runMs;

      isMoving = false;
      moveDirection = 0;
      testServo.writeMicroseconds(NEUTRAL_US);

      Serial.print("[FORWARD] STOP ms=");
      Serial.print(stopMs);
      Serial.print(" | moved_ms=");
      Serial.print(runMs);
      Serial.print(" | total_forward_ms=");
      Serial.println(totalForwardTimeMs);
    }
  }

  // Debounce backward button.
  if (currentRawBackward != rawBackwardPressed) {
    rawBackwardPressed = currentRawBackward;
    lastBackwardDebounceMs = millis();
  }

  if ((millis() - lastBackwardDebounceMs) >= DEBOUNCE_MS && stableBackwardPressed != rawBackwardPressed) {
    stableBackwardPressed = rawBackwardPressed;

    if (stableBackwardPressed && !isMoving) {
      isMoving = true;
      moveDirection = -1;
      moveStartMs = millis();
      testServo.writeMicroseconds(REVERSE_US);
      Serial.print("[BACKWARD] START ms=");
      Serial.println(moveStartMs);
    } else if (!stableBackwardPressed && moveDirection == -1) {
      unsigned long stopMs = millis();
      unsigned long runMs = stopMs - moveStartMs;
      totalBackwardTimeMs += runMs;

      isMoving = false;
      moveDirection = 0;
      testServo.writeMicroseconds(NEUTRAL_US);

      Serial.print("[BACKWARD] STOP ms=");
      Serial.print(stopMs);
      Serial.print(" | moved_ms=");
      Serial.print(runMs);
      Serial.print(" | total_backward_ms=");
      Serial.println(totalBackwardTimeMs);
    }
  }
}
