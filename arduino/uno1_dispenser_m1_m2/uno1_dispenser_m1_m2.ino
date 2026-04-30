// Uno 1: Dispenser A (Motor 1 + Motor 2 with IR sensors)
// Serial link to ESP32 for commands
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
//   IR1 D0 -> A0
//   IR2 D0 -> A1
//
//   Serial to ESP32: D0/D1 (RX/TX)

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t IR1_DIGITAL_PIN = A0;
constexpr uint8_t IR2_DIGITAL_PIN = A1;
constexpr bool ACTIVE_LOW = true;
constexpr unsigned long IR_PRINT_INTERVAL_MS = 500;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr bool DISABLE_IR_STOP_FOR_TEST = true;

constexpr unsigned long RUN_SEGMENT_M1_FORWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M1_BACKWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M2_FORWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_M2_BACKWARD_MS = 10000;

constexpr unsigned long STEP_INTERVAL_US = 2200;
constexpr unsigned long STARTUP_GRACE_MS = 2000;

constexpr bool TEST_ONLY_MOTOR2 = true;

constexpr uint8_t M1_IN1_PIN = 2;
constexpr uint8_t M1_IN2_PIN = 3;
constexpr uint8_t M1_IN3_PIN = 4;
constexpr uint8_t M1_IN4_PIN = 5;

constexpr uint8_t M2_IN1_PIN = 6;
constexpr uint8_t M2_IN2_PIN = 7;
constexpr uint8_t M2_IN3_PIN = 8;
constexpr uint8_t M2_IN4_PIN = 9;

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
bool latchedStop = false;
unsigned long startupMs = 0;
unsigned long detectHoldAccumMs = 0;

MotorState motor1 = {
  M1_IN1_PIN, M1_IN2_PIN, M1_IN3_PIN, M1_IN4_PIN,
  0,
  MOTION_FORWARD,
  0,
  RUN_SEGMENT_M1_FORWARD_MS,
};

MotorState motor2 = {
  M2_IN1_PIN, M2_IN2_PIN, M2_IN3_PIN, M2_IN4_PIN,
  0,
  MOTION_FORWARD,
  0,
  RUN_SEGMENT_M2_FORWARD_MS,
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

bool readStopDetected(int raw1, int raw2) {
  const bool detected1 = readDetectedFromRaw(raw1);
  const bool detected2 = readDetectedFromRaw(raw2);
  return TEST_ONLY_MOTOR2 ? detected2 : (detected1 || detected2);
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

  pinMode(IR1_DIGITAL_PIN, INPUT);
  pinMode(IR2_DIGITAL_PIN, INPUT);

  releaseCoils(motor1);
  releaseCoils(motor2);
  startupMs = millis();
  lastMotionUpdateMs = startupMs;

  Serial.println(F("Uno1 Dispenser A ready (M1 + M2)"));
  if (TEST_ONLY_MOTOR2) {
    Serial.println(F("TEST MODE: Motor 2 only, Motor 1 halted"));
  } else {
    Serial.println(F("NORMAL MODE: Both motors active"));
  }
  if (DISABLE_IR_STOP_FOR_TEST) {
    Serial.println(F("IR stop disabled for Motor 2 bring-up test"));
  }
  Serial.println(F("Starts running now; M2 alternates 10s FWD/10s BWD until IR2 detects"));
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long dtMs = nowMs - lastMotionUpdateMs;
  lastMotionUpdateMs = nowMs;

  int digitalRaw1 = digitalRead(IR1_DIGITAL_PIN);
  int digitalRaw2 = digitalRead(IR2_DIGITAL_PIN);
  bool detected = readStopDetected(digitalRaw1, digitalRaw2);

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
    releaseCoils(motor1);
    releaseCoils(motor2);
  } else {
    if (!TEST_ONLY_MOTOR2) {
      motor1.runSegmentMs =
        (motor1.motion == MOTION_FORWARD) ? RUN_SEGMENT_M1_FORWARD_MS : RUN_SEGMENT_M1_BACKWARD_MS;
      updateMotorDirection(motor1, dtMs);

      motor2.runSegmentMs =
        (motor2.motion == MOTION_FORWARD) ? RUN_SEGMENT_M2_FORWARD_MS : RUN_SEGMENT_M2_BACKWARD_MS;
      updateMotorDirection(motor2, dtMs);
    } else {
      releaseCoils(motor1);
      motor2.runSegmentMs =
        (motor2.motion == MOTION_FORWARD) ? RUN_SEGMENT_M2_FORWARD_MS : RUN_SEGMENT_M2_BACKWARD_MS;
      updateMotorDirection(motor2, dtMs);
    }

    unsigned long nowUs = micros();

    if (!TEST_ONLY_MOTOR2 && (nowUs - lastStepM1Micros >= STEP_INTERVAL_US)) {
      if (motor1.motion == MOTION_FORWARD) {
        stepForwardOnce(motor1);
      } else {
        stepBackwardOnce(motor1);
      }
      lastStepM1Micros = nowUs;
    }

    if (nowUs - lastStepM2Micros >= STEP_INTERVAL_US) {
      if (motor2.motion == MOTION_FORWARD) {
        stepForwardOnce(motor2);
      } else {
        stepBackwardOnce(motor2);
      }
      lastStepM2Micros = nowUs;
    }
  }

  if (nowMs - lastIrPrintMs >= IR_PRINT_INTERVAL_MS) {
    lastIrPrintMs = nowMs;
    const bool detected1 = readDetectedFromRaw(digitalRaw1);
    const bool detected2 = readDetectedFromRaw(digitalRaw2);
    Serial.print(F("IR1="));
    Serial.print(digitalRaw1);
    Serial.print(F("("));
    Serial.print(detected1 ? F("DET") : F("CLR"));
    Serial.print(F(")"));
    Serial.print(F(" IR2="));
    Serial.print(digitalRaw2);
    Serial.print(F("("));
    Serial.print(detected2 ? F("DET") : F("CLR"));
    Serial.print(F(")"));
    Serial.print(F(" detect="));
    Serial.print(detected ? F("YES") : F("NO"));
    Serial.print(F(" hitMs="));
    Serial.print(detectHoldAccumMs);
    Serial.print(F(" M1="));
    if (latchedStop) {
      Serial.println(F("HALTED"));
    } else {
      Serial.print(motor1.motion == MOTION_FORWARD ? F("FWD") : F("BWD"));
      Serial.print(F(" M2="));
      if (TEST_ONLY_MOTOR2) {
        Serial.println(F("ACTIVE(TEST)"));
      } else {
        Serial.println(motor2.motion == MOTION_FORWARD ? F("FWD") : F("BWD"));
      }
    }
  }
}
