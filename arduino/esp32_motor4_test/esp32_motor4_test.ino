// ESP32 Motor 4 test
// Behavior: 10s forward, 10s backward, repeat until IR detects object.

namespace {

constexpr long SERIAL_BAUD = 115200;

// Motor 4 via ULN2003
constexpr uint8_t M4_IN1_PIN = 18;
constexpr uint8_t M4_IN2_PIN = 19;
constexpr uint8_t M4_IN3_PIN = 23;
constexpr uint8_t M4_IN4_PIN = 13;

// Set this to the ESP32 GPIO where Motor 4 IR D0 is wired.
constexpr uint8_t IR4_DIGITAL_PIN = 27;
constexpr bool ACTIVE_LOW = true;

constexpr unsigned long RUN_SEGMENT_FORWARD_MS = 10000;
constexpr unsigned long RUN_SEGMENT_BACKWARD_MS = 10000;
constexpr unsigned long STEP_INTERVAL_US = 2200;
constexpr unsigned long STARTUP_GRACE_MS = 2000;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr unsigned long IR_PRINT_INTERVAL_MS = 500;
constexpr bool DEBUG_MODE = true;
constexpr bool COIL_CHASE_MODE = false;
constexpr unsigned long COIL_HOLD_MS = 700;

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
  int sequenceIndex;
  MotionDirection motion;
  unsigned long segmentElapsedMs;
  unsigned long runSegmentMs;
};

MotorState motor4 = {0, MOTION_FORWARD, 0, RUN_SEGMENT_FORWARD_MS};

unsigned long startupMs = 0;
unsigned long lastMotionUpdateMs = 0;
unsigned long lastStepMicros = 0;
unsigned long lastIrPrintMs = 0;
unsigned long detectHoldAccumMs = 0;
unsigned long lastCoilStepMs = 0;
uint8_t coilIndex = 0;
bool latchedStop = false;

void writeCoils(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  digitalWrite(M4_IN1_PIN, a);
  digitalWrite(M4_IN2_PIN, b);
  digitalWrite(M4_IN3_PIN, c);
  digitalWrite(M4_IN4_PIN, d);
}

void releaseCoils() {
  writeCoils(0, 0, 0, 0);
}

void stepForwardOnce(MotorState& motor) {
  writeCoils(
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
    HALF_STEP_SEQUENCE[motor.sequenceIndex][0],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][1],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][2],
    HALF_STEP_SEQUENCE[motor.sequenceIndex][3]
  );
}

bool isDetected(int rawValue) {
  return ACTIVE_LOW ? (rawValue == LOW) : (rawValue == HIGH);
}

void updateDirection(MotorState& motor, unsigned long dtMs) {
  motor.segmentElapsedMs += dtMs;
  while (motor.segmentElapsedMs >= motor.runSegmentMs) {
    motor.segmentElapsedMs -= motor.runSegmentMs;
    motor.motion = (motor.motion == MOTION_FORWARD) ? MOTION_BACKWARD : MOTION_FORWARD;
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(M4_IN1_PIN, OUTPUT);
  pinMode(M4_IN2_PIN, OUTPUT);
  pinMode(M4_IN3_PIN, OUTPUT);
  pinMode(M4_IN4_PIN, OUTPUT);
  pinMode(IR4_DIGITAL_PIN, INPUT);

  releaseCoils();
  startupMs = millis();
  lastMotionUpdateMs = startupMs;

  Serial.println(F("ESP32 Motor 4 test ready"));
  if (COIL_CHASE_MODE) {
    Serial.println(F("COIL CHASE MODE: energizing one coil at a time"));
  } else {
    Serial.println(F("10s FWD / 10s BWD until IR detects"));
  }
}

void loop() {
  unsigned long nowMs = millis();
  unsigned long dtMs = nowMs - lastMotionUpdateMs;
  lastMotionUpdateMs = nowMs;

  int raw = digitalRead(IR4_DIGITAL_PIN);
  bool detected = isDetected(raw);

  if (!latchedStop) {
    bool inStartupGrace = (nowMs - startupMs) < STARTUP_GRACE_MS;
    if (inStartupGrace || DEBUG_MODE) {
      detectHoldAccumMs = 0;
    } else if (detected) {
      detectHoldAccumMs += dtMs;
      if (detectHoldAccumMs >= DETECT_HOLD_MS) {
        latchedStop = true;
        Serial.println(F("*** IR DETECTED - MOTOR 4 STOPPED ***"));
      }
    } else {
      detectHoldAccumMs = 0;
    }
  }

  if (latchedStop) {
    releaseCoils();
  } else {
    if (COIL_CHASE_MODE) {
      if (nowMs - lastCoilStepMs >= COIL_HOLD_MS) {
        coilIndex = (coilIndex + 1) & 0x03;
        if (coilIndex == 0) {
          writeCoils(1, 0, 0, 0);
        } else if (coilIndex == 1) {
          writeCoils(0, 1, 0, 0);
        } else if (coilIndex == 2) {
          writeCoils(0, 0, 1, 0);
        } else {
          writeCoils(0, 0, 0, 1);
        }
        lastCoilStepMs = nowMs;
      }
    } else {
      motor4.runSegmentMs =
        (motor4.motion == MOTION_FORWARD) ? RUN_SEGMENT_FORWARD_MS : RUN_SEGMENT_BACKWARD_MS;
      updateDirection(motor4, dtMs);

      unsigned long nowUs = micros();
      if (nowUs - lastStepMicros >= STEP_INTERVAL_US) {
        if (motor4.motion == MOTION_FORWARD) {
          stepForwardOnce(motor4);
        } else {
          stepBackwardOnce(motor4);
        }
        lastStepMicros = nowUs;
      }
    }
  }

  if (nowMs - lastIrPrintMs >= IR_PRINT_INTERVAL_MS) {
    lastIrPrintMs = nowMs;
    Serial.print(F("IR="));
    Serial.print(raw);
    Serial.print(F(" M4="));
    if (latchedStop) {
      Serial.println(F("HALTED"));
    } else if (COIL_CHASE_MODE) {
      Serial.print(F("COIL="));
      Serial.println(coilIndex + 1);
    } else {
      Serial.println(motor4.motion == MOTION_FORWARD ? F("FWD") : F("BWD"));
    }
  }
}
