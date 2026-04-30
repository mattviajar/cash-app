#include <AccelStepper.h>

// 28BYJ-48 + ULN2003 (HALF4WIRE gives smoother motion)
AccelStepper stepper(AccelStepper::HALF4WIRE, 25, 27, 26, 14);

const int BTN_PIN = 32;
const float RUN_SPEED = 500.0;   // tune 250..700
const float MAX_SPEED = 900.0;

void setup() {
  pinMode(BTN_PIN, INPUT_PULLUP);

  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setSpeed(0);
}

void loop() {
  bool pressed = (digitalRead(BTN_PIN) == LOW);

  if (pressed) {
    stepper.setSpeed(RUN_SPEED);  // forward while pressed
  } else {
    stepper.setSpeed(0);          // stop when released
  }

  stepper.runSpeed();             // non-blocking
}