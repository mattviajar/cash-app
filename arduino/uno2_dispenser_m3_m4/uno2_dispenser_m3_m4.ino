// Uno 2: Dispenser B (Motor 3 + Motor 4 with IR sensors)
// Serial link to ESP32 for commands
//
// Wiring:
//   Motor 3 ULN2003 IN1 -> D2
//   Motor 3 ULN2003 IN2 -> D3
//   Motor 3 ULN2003 IN3 -> D4
//   Motor 3 ULN2003 IN4 -> D5
//
//   Motor 4 ULN2003 IN1 -> D6
//   Motor 4 ULN2003 IN2 -> D7
//   Motor 4 ULN2003 IN3 -> D8
//   Motor 4 ULN2003 IN4 -> D9
//
//   IR3 D0 -> A0
//   IR4 D0 -> A1
//
//   Serial to ESP32: D0/D1 (RX/TX)

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t IR3_DIGITAL_PIN = A0;
constexpr uint8_t IR4_DIGITAL_PIN = A1;
constexpr bool ACTIVE_LOW = true;
constexpr unsigned long IR_PRINT_INTERVAL_MS = 500;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr bool DISABLE_IR_STOP_FOR_TEST = true;

constexpr unsigned long RUN_SEGMENT_M3_FORWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M3_BACKWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M4_FORWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M4_BACKWARD_MS = 10000;

constexpr unsigned long STEP_INTERVAL_US = 2200;
constexpr unsigned long STARTUP_GRACE_MS = 2000;

constexpr bool TEST_ONLY_MOTOR3 = true;

constexpr uint8_t M3_IN1_PIN = 2;
constexpr uint8_t M3_IN2_PIN = 3;
constexpr uint8_t M3_IN3_PIN = 4;
constexpr uint8_t M3_IN4_PIN = 5;

constexpr uint8_t M4_IN1_PIN = 6;
constexpr uint8_t M4_IN2_PIN = 7;
constexpr uint8_t M4_IN3_PIN = 8;
constexpr uint8_t M4_IN4_PIN = 9;

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
unsigned long lastStepM3Micros = 0;
unsigned long lastStepM4Micros = 0;
bool latchedStop = false;
unsigned long startupMs = 0;
unsigned long detectHoldAccumMs = 0;

MotorState motor3 = {
  M3_IN1_PIN, M3_IN2_PIN, M3_IN3_PIN, M3_IN4_PIN,
  0,
  MOTION_FORWARD,
  0,
  RUN_SEGMENT_M3_FORWARD_MS,
};

MotorState motor4 = {
  M4_IN1_PIN, M4_IN2_PIN, M4_IN3_PIN, M4_IN4_PIN,
  0,
  MOTION_FORWARD,
  0,
  RUN_SEGMENT_M4_FORWARD_MS,
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

bool readDetectedFromRaw(int rawValue) {
  return ACTIVE_LOW ? (rawValue == LOW) : (rawValue == HIGH);
}

bool readStopDetected(int raw3, int raw4) {
  const bool detected3 = readDetectedFromRaw(raw3);
  const bool detected4 = readDetectedFromRaw(raw4);
  return TEST_ONLY_MOTOR3 ? detected3 : (detected3 || detected4);
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);

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

  releaseCoils(motor3);
  releaseCoils(motor4);
  startupMs = millis();
  lastMotionUpdateMs = startupMs;

  Serial.println(F("Uno2 Dispenser B ready (M3 + M4)"));
  if (TEST_ONLY_MOTOR3) {
    Serial.println(F("TEST MODE: Motor 3 only, Motor 4 halted"));
  } else {
    Serial.println(F("NORMAL MODE: Both motors active"));
  }
  if (DISABLE_IR_STOP_FOR_TEST) {
    Serial.println(F("IR stop disabled for Motor 3 bring-up test"));
  }
  Serial.println(F("Starts running now; M3 alternates 10s FWD/10s BWD until IR3 detects"));
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long dtMs = nowMs - lastMotionUpdateMs;
  lastMotionUpdateMs = nowMs;

  int digitalRaw3 = digitalRead(IR3_DIGITAL_PIN);
  int digitalRaw4 = digitalRead(IR4_DIGITAL_PIN);
  bool detected = readStopDetected(digitalRaw3, digitalRaw4);

  if (!latchedStop && !DISABLE_IR_STOP_FOR_TEST) {
    bool inStartupGrace = (nowMs - startupMs) < STARTUP_GRACE_MS;

    if (inStartupGrace) {
      detectHoldAccumMs = 0;
    } else if (detected) {
      detectHoldAccumMs += dtMs;
      if (detectHoldAccumMs >= DETECT_HOLD_MS) {
        latchedStop = true;
        Serial.println(F("*** IR DETECTED - MOTOR STOPPED ***"));
      }
    } else {
      detectHoldAccumMs = 0;
    }
  }

  if (latchedStop) {
    releaseCoils(motor3);
    releaseCoils(motor4);
  } else {
    if (!TEST_ONLY_MOTOR3) {
      motor3.runSegmentMs =
        (motor3.motion == MOTION_FORWARD) ? RUN_SEGMENT_M3_FORWARD_MS : RUN_SEGMENT_M3_BACKWARD_MS;
      updateMotorDirection(motor3, dtMs);

      motor4.runSegmentMs =
        (motor4.motion == MOTION_FORWARD) ? RUN_SEGMENT_M4_FORWARD_MS : RUN_SEGMENT_M4_BACKWARD_MS;
      updateMotorDirection(motor4, dtMs);
    } else {
      releaseCoils(motor4);
      motor3.runSegmentMs =
        (motor3.motion == MOTION_FORWARD) ? RUN_SEGMENT_M3_FORWARD_MS : RUN_SEGMENT_M3_BACKWARD_MS;
      updateMotorDirection(motor3, dtMs);
    }

    unsigned long nowUs = micros();

    if (nowUs - lastStepM3Micros >= STEP_INTERVAL_US) {
      if (motor3.motion == MOTION_FORWARD) {
        stepForwardOnce(motor3);
      } else {
        stepBackwardOnce(motor3);
      }
      lastStepM3Micros = nowUs;
    }

    if (!TEST_ONLY_MOTOR3 && (nowUs - lastStepM4Micros >= STEP_INTERVAL_US)) {
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
    const bool detected3 = readDetectedFromRaw(digitalRaw3);
    const bool detected4 = readDetectedFromRaw(digitalRaw4);
    Serial.print(F("IR3="));
    Serial.print(digitalRaw3);
    Serial.print(F("("));
    Serial.print(detected3 ? F("DET") : F("CLR"));
    Serial.print(F(")"));
    Serial.print(F(" IR4="));
    Serial.print(digitalRaw4);
    Serial.print(F("("));
    Serial.print(detected4 ? F("DET") : F("CLR"));
    Serial.print(F(")"));
    Serial.print(F(" detect="));
    Serial.print(detected ? F("YES") : F("NO"));
    Serial.print(F(" hitMs="));
    Serial.print(detectHoldAccumMs);
    Serial.print(F(" M3="));
    if (latchedStop) {
      Serial.println(F("HALTED"));
    } else {
      Serial.print(motor3.motion == MOTION_FORWARD ? F("FWD") : F("BWD"));
      Serial.print(F(" M4="));
      if (TEST_ONLY_MOTOR3) {
        Serial.println(F("HALTED(TEST)"));
      } else {
        Serial.println(motor4.motion == MOTION_FORWARD ? F("FWD") : F("BWD"));
      }
    }
  }
}
