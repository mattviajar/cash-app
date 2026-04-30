// Four stepper + 4-pin IR digital detect
//
// Wiring:
//   Motor 1 ULN2003 IN1 -> D2
//   Motor 1 ULN2003 IN2 -> D3
//   Motor 1 ULN2003 IN3 -> D4
//   Motor 1 ULN2003 IN4 -> D5
//
//   Motor 2 ULN2003 IN1 -> D6
//   Motor 2 ULN2003 IN2 -> D7
//   Motor 2 ULN2003 IN3 -> D8
//   Motor 2 ULN2003 IN4 -> D9
//
//   Motor 3 ULN2003 IN1 -> D10
//   Motor 3 ULN2003 IN2 -> D11
//   Motor 3 ULN2003 IN3 -> D12
//   Motor 3 ULN2003 IN4 -> D13
//
//   Motor 4 ULN2003 IN1 -> A0
//   Motor 4 ULN2003 IN2 -> A1
//   Motor 4 ULN2003 IN3 -> A2
//   Motor 4 ULN2003 IN4 -> A3
//
//   IR3 sensor D0 -> A4 (digital detect)
//   IR4 sensor D0 -> A5 (digital detect)
//   IR sensor VCC -> 5V
//   IR sensor GND -> GND
//
// Behavior:
//   No object: each motor runs FORWARD/BACKWARD on its own segment timer.
//   Starts running immediately at boot.
//   After IR has seen CLEAR at least once, a detect event latches STOP for both motors.

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t IR3_DIGITAL_PIN = A4;
constexpr uint8_t IR4_DIGITAL_PIN = A5;
constexpr bool ACTIVE_LOW = true;
constexpr unsigned long IR_PRINT_INTERVAL_MS = 500;
constexpr unsigned long RUN_SEGMENT_M1_MS = 13000;
constexpr unsigned long RUN_SEGMENT_M2_FORWARD_MS = 11000;
constexpr unsigned long RUN_SEGMENT_M2_BACKWARD_MS = 9000;
constexpr unsigned long RUN_SEGMENT_M3_FORWARD_MS = 8000;
constexpr unsigned long RUN_SEGMENT_M3_BACKWARD_MS = 7000;
constexpr unsigned long RUN_SEGMENT_M4_FORWARD_MS = 8000;
constexpr unsigned long RUN_SEGMENT_M4_BACKWARD_MS = 8000;
constexpr unsigned long STEP_INTERVAL_M1_US = 1800;
const unsigned long STEP_INTERVAL_M2_US = 1800;
constexpr unsigned long STEP_INTERVAL_M3_US = 1800;
constexpr unsigned long STEP_INTERVAL_M4_US = 1800;
constexpr unsigned long STARTUP_GRACE_MS = 2000;
constexpr unsigned long DETECT_HOLD_MS = 30;
constexpr bool TEST_ONLY_MOTOR3 = true;
constexpr bool TEST_MOTOR2_FORWARD_ONLY = false;
constexpr bool MOTOR2_INVERT_DIRECTION = false;
constexpr bool TEST_DISABLE_IR_LATCH = false;

constexpr uint8_t M1_IN1_PIN = 2;
constexpr uint8_t M1_IN2_PIN = 3;
constexpr uint8_t M1_IN3_PIN = 4;
constexpr uint8_t M1_IN4_PIN = 5;

constexpr uint8_t M2_IN1_PIN = 6;
constexpr uint8_t M2_IN2_PIN = 7;
constexpr uint8_t M2_IN3_PIN = 8;
constexpr uint8_t M2_IN4_PIN = 9;

constexpr uint8_t M3_IN1_PIN = 10;
constexpr uint8_t M3_IN2_PIN = 11;
constexpr uint8_t M3_IN3_PIN = 12;
constexpr uint8_t M3_IN4_PIN = 13;

constexpr uint8_t M4_IN1_PIN = A0;
constexpr uint8_t M4_IN2_PIN = A1;
constexpr uint8_t M4_IN3_PIN = A2;
constexpr uint8_t M4_IN4_PIN = A3;

const uint8_t HALF_STEP_SEQUENCE[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1},
};

enum MotionDirection {
  MOTION_FORWARD,
  MOTION_BACKWARD,
};

struct MotorState {
  uint8_t in1;
  uint8_t in2;
  uint8_t in3;
  uint8_t in4;
  int sequenceIndex;
  MotionDirection motion;
  unsigned long segmentElapsedMs;
  unsigned long runSegmentMs;
};

unsigned long lastIrPrintMs = 0;
unsigned long lastMotionUpdateMs = 0;
unsigned long lastStepM1Micros = 0;
unsigned long lastStepM2Micros = 0;
unsigned long lastStepM3Micros = 0;
unsigned long lastStepM4Micros = 0;
bool latchedStop = false;
bool sensorArmed = false;
unsigned long startupMs = 0;
unsigned long detectHoldAccumMs = 0;

MotorState motor1 = {
  M1_IN1_PIN, M1_IN2_PIN, M1_IN3_PIN, M1_IN4_PIN,
  0,
  MOTION_BACKWARD,
  0,
  RUN_SEGMENT_M1_MS,
};

MotorState motor2 = {
  M2_IN1_PIN, M2_IN2_PIN, M2_IN3_PIN, M2_IN4_PIN,
  0,
  MOTION_BACKWARD,
  0,
  RUN_SEGMENT_M2_BACKWARD_MS,
};

MotorState motor3 = {
  M3_IN1_PIN, M3_IN2_PIN, M3_IN3_PIN, M3_IN4_PIN,
  0,
  MOTION_BACKWARD,
  0,
  RUN_SEGMENT_M3_BACKWARD_MS,
};

MotorState motor4 = {
  M4_IN1_PIN, M4_IN2_PIN, M4_IN3_PIN, M4_IN4_PIN,
  0,
  MOTION_BACKWARD,
  0,
  RUN_SEGMENT_M4_BACKWARD_MS,
};

void writeCoils(const MotorState& motor, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  digitalWrite(motor.in1, a);
  digitalWrite(motor.in2, b);
  digitalWrite(motor.in3, c);
  digitalWrite(motor.in4, d);
}

void releaseCoils(const MotorState& motor) {
  writeCoils(motor, 0, 0, 0, 0);
}

void stepForwardOnce(MotorState& motor) {
  writeCoils(
    motor,
    HALF_STEP_SEQUENCE[motor.sequenceIndex][0],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][1],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][2],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][3]
  );
  motor.sequenceIndex = (motor.sequenceIndex + 1) & 0x07;
}

void stepBackwardOnce(MotorState& motor) {
  motor.sequenceIndex = (motor.sequenceIndex + 7) & 0x07;
  writeCoils(
    motor,
    HALF_STEP_SEQUENCE[motor.sequenceIndex][0],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][1],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][2],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][3]
  );
}

void updateMotorDirection(MotorState& motor, unsigned long dtMs) {
  motor.segmentElapsedMs += dtMs;
  while (motor.segmentElapsedMs >= motor.runSegmentMs) {
    motor.segmentElapsedMs -= motor.runSegmentMs;
    motor.motion = (motor.motion == MOTION_FORWARD) ? MOTION_BACKWARD : MOTION_FORWARD;
  }
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(motor1.in1, OUTPUT);
  pinMode(motor1.in2, OUTPUT);
  pinMode(motor1.in3, OUTPUT);
  pinMode(motor1.in4, OUTPUT);

  pinMode(motor2.in1, OUTPUT);
  pinMode(motor2.in2, OUTPUT);
  pinMode(motor2.in3, OUTPUT);
  pinMode(motor2.in4, OUTPUT);

  pinMode(motor3.in1, OUTPUT);
  pinMode(motor3.in2, OUTPUT);
  pinMode(motor3.in3, OUTPUT);
  pinMode(motor3.in4, OUTPUT);

  pinMode(motor4.in1, OUTPUT);
  pinMode(motor4.in2, OUTPUT);
  pinMode(motor4.in3, OUTPUT);
  pinMode(motor4.in4, OUTPUT);

  pinMode(IR3_DIGITAL_PIN, INPUT);
  pinMode(IR4_DIGITAL_PIN, INPUT);
  releaseCoils(motor1);
  releaseCoils(motor2);
  releaseCoils(motor3);
  releaseCoils(motor4);
  startupMs = millis();
  lastMotionUpdateMs = startupMs;

  Serial.println(F("Four stepper + IR digital detect ready"));
  Serial.println(F("Motor 3 active test mode; motor 1, 2 and 4 halted"));
  Serial.println(F("Starts running now; stop latches after first clear->detect event"));
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long dtMs = nowMs - lastMotionUpdateMs;
  lastMotionUpdateMs = nowMs;

  int digitalRaw3 = digitalRead(IR3_DIGITAL_PIN);
  int digitalRaw4 = digitalRead(IR4_DIGITAL_PIN);
  bool detected3 = ACTIVE_LOW ? (digitalRaw3 == LOW) : (digitalRaw3 == HIGH);
  bool detected4 = ACTIVE_LOW ? (digitalRaw4 == LOW) : (digitalRaw4 == HIGH);
  bool detected = detected3 || detected4;

  if (!latchedStop && !TEST_DISABLE_IR_LATCH) {
    bool inStartupGrace = (nowMs - startupMs) < STARTUP_GRACE_MS;

    if (!detected) {
      sensorArmed = true;
      detectHoldAccumMs = 0;
    }

    if (sensorArmed && detected && !inStartupGrace) {
      detectHoldAccumMs += dtMs;
      if (detectHoldAccumMs >= DETECT_HOLD_MS) {
        latchedStop = true;
      }
    } else if (!detected || inStartupGrace) {
      detectHoldAccumMs = 0;
    }
  }

  if (latchedStop) {
    releaseCoils(motor1);
    releaseCoils(motor2);
    releaseCoils(motor3);
    releaseCoils(motor4);
  } else {
    if (TEST_ONLY_MOTOR3) {
      releaseCoils(motor1);
      releaseCoils(motor2);
      releaseCoils(motor4);
    } else {
      updateMotorDirection(motor1, dtMs);
      if (TEST_MOTOR2_FORWARD_ONLY) {
        motor2.motion = MOTION_FORWARD;
        motor2.segmentElapsedMs = 0;
      } else {
        motor2.runSegmentMs =
          (motor2.motion == MOTION_FORWARD) ? RUN_SEGMENT_M2_FORWARD_MS : RUN_SEGMENT_M2_BACKWARD_MS;
        updateMotorDirection(motor2, dtMs);
      }

      motor4.runSegmentMs =
        (motor4.motion == MOTION_FORWARD) ? RUN_SEGMENT_M4_FORWARD_MS : RUN_SEGMENT_M4_BACKWARD_MS;
      updateMotorDirection(motor4, dtMs);
    }

    motor3.runSegmentMs =
      (motor3.motion == MOTION_FORWARD) ? RUN_SEGMENT_M3_FORWARD_MS : RUN_SEGMENT_M3_BACKWARD_MS;
    updateMotorDirection(motor3, dtMs);

    unsigned long nowUs = micros();

    if (!TEST_ONLY_MOTOR3 && (nowUs - lastStepM1Micros >= STEP_INTERVAL_M1_US)) {
      if (motor1.motion == MOTION_FORWARD) {
        stepForwardOnce(motor1);
      } else {
        stepBackwardOnce(motor1);
      }
      lastStepM1Micros = nowUs;
    }

    if (!TEST_ONLY_MOTOR3) {
      MotionDirection motor2EffectiveMotion = motor2.motion;
      if (MOTOR2_INVERT_DIRECTION) {
        motor2EffectiveMotion = (motor2.motion == MOTION_FORWARD) ? MOTION_BACKWARD : MOTION_FORWARD;
      }

      if (nowUs - lastStepM2Micros >= STEP_INTERVAL_M2_US) {
        if (motor2EffectiveMotion == MOTION_FORWARD) {
          stepForwardOnce(motor2);
        } else {
          stepBackwardOnce(motor2);
        }
        lastStepM2Micros = nowUs;
      }
    }

    if (nowUs - lastStepM3Micros >= STEP_INTERVAL_M3_US) {
      if (motor3.motion == MOTION_FORWARD) {
        stepForwardOnce(motor3);
      } else {
        stepBackwardOnce(motor3);
      }
      lastStepM3Micros = nowUs;
    }

    if (!TEST_ONLY_MOTOR3 && (nowUs - lastStepM4Micros >= STEP_INTERVAL_M4_US)) {
      if (motor4.motion == MOTION_FORWARD) {
        stepForwardOnce(motor4);
      } else {
        stepBackwardOnce(motor4);
      }
      lastStepM4Micros = nowUs;
    }
  }

  if (nowMs - lastIrPrintMs >= IR_PRINT_INTERVAL_MS) {
    lastIrPrintMs = nowMs;
    Serial.print(F("D0_3="));
    Serial.print(digitalRaw3);
    Serial.print(F(" D0_4="));
    Serial.print(digitalRaw4);
    Serial.print(F(" detected_any="));
    Serial.print(detected ? F("YES") : F("NO"));
    Serial.print(F(" m1="));
    if (latchedStop) {
      Serial.println(F("HALTED"));
    } else {
      if (TEST_ONLY_MOTOR3) {
        Serial.print(F("HALTED(TEST)"));
      } else {
        Serial.print(motor1.motion == MOTION_FORWARD ? F("FORWARD") : F("BACKWARD"));
      }
      Serial.print(F(" m2="));
      if (TEST_ONLY_MOTOR3) {
        Serial.print(F("HALTED(TEST)"));
      } else {
        Serial.print(motor2.motion == MOTION_FORWARD ? F("FORWARD") : F("BACKWARD"));
      }
      Serial.print(F(" m3="));
      Serial.print(motor3.motion == MOTION_FORWARD ? F("FORWARD") : F("BACKWARD"));
      Serial.print(F(" m4="));
      if (TEST_ONLY_MOTOR3) {
        Serial.println(F("HALTED(TEST)"));
      } else {
        Serial.println(motor4.motion == MOTION_FORWARD ? F("FORWARD") : F("BACKWARD"));
      }
    }
  }
}