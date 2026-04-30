// Arduino Uno: Motors 1, 2, 3 test
// Behavior: Each motor runs 10s forward, 10s backward, repeating until IR detects object.
// Change TEST_MOTOR to test individual motors.

namespace {

constexpr long SERIAL_BAUD = 115200;

// Motor pins
constexpr uint8_t M1_IN1 = 2, M1_IN2 = 3, M1_IN3 = 4, M1_IN4 = 5;
constexpr uint8_t M2_IN1 = 6, M2_IN2 = 7, M2_IN3 = 8, M2_IN4 = 9;
constexpr uint8_t M3_IN1 = 10, M3_IN2 = 11, M3_IN3 = 12, M3_IN4 = 13;

// IR pins
constexpr uint8_t IR1_PIN = A0;
constexpr uint8_t IR2_PIN = 15;
constexpr uint8_t IR3_PIN = 16;
constexpr bool ACTIVE_LOW = true;

// Test mode: set which motor to test (1, 2, or 3)
constexpr int TEST_MOTOR = 3;
constexpr bool RUN_BOTH_M1_M2 = false;

// Timing
constexpr unsigned long RUN_SEGMENT_MS = 10000;
constexpr unsigned long STEP_INTERVAL_M1_US = 2400;
constexpr unsigned long STEP_INTERVAL_M2_US = 2600;
constexpr unsigned long STARTUP_GRACE_MS = 2000;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr unsigned long IR_PRINT_INTERVAL_MS = 500;
constexpr bool DEBUG_MODE = true;
constexpr bool COIL_CHASE_MODE = false;
constexpr bool COIL_CHASE_BOTH_M1_M2 = false;
constexpr bool DUAL_TEST_DISABLE_IR_STOP = true;
constexpr unsigned long COIL_HOLD_MS = 700;

const uint8_t HALF_STEP[8][4] = {
  {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
  {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1},
};

enum Direction { FWD, BWD };

struct Motor {
  uint8_t in1, in2, in3, in4;
  int seqIdx;
  Direction dir;
  unsigned long elapsedMs, runMs;
};

Motor motor1 = {M1_IN1, M1_IN2, M1_IN3, M1_IN4, 0, FWD, 0, RUN_SEGMENT_MS};
Motor motor2 = {M2_IN1, M2_IN2, M2_IN3, M2_IN4, 0, FWD, 0, RUN_SEGMENT_MS};
Motor motor3 = {M3_IN1, M3_IN2, M3_IN3, M3_IN4, 0, FWD, 0, RUN_SEGMENT_MS};

unsigned long startupMs, lastUpdateMs, lastStepUs, lastPrintMs;
unsigned long lastStepM1Us = 0;
unsigned long lastStepM2Us = 0;
unsigned long lastCoilStepMs = 0;
unsigned long detectAccumMs = 0;
bool stopped = false;
uint8_t coilIndex = 0;

void writeCoils(const Motor& m, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  digitalWrite(m.in1, a); digitalWrite(m.in2, b);
  digitalWrite(m.in3, c); digitalWrite(m.in4, d);
}

void release(const Motor& m) { writeCoils(m, 0, 0, 0, 0); }

void stepFwd(Motor& m) {
  writeCoils(m, HALF_STEP[m.seqIdx][0], HALF_STEP[m.seqIdx][1],
             HALF_STEP[m.seqIdx][2], HALF_STEP[m.seqIdx][3]);
  m.seqIdx = (m.seqIdx + 1) & 7;
}

void stepBwd(Motor& m) {
  m.seqIdx = (m.seqIdx + 7) & 7;
  writeCoils(m, HALF_STEP[m.seqIdx][0], HALF_STEP[m.seqIdx][1],
             HALF_STEP[m.seqIdx][2], HALF_STEP[m.seqIdx][3]);
}

bool isDetected(int raw) {
  return ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

void updateDir(Motor& m, unsigned long dtMs) {
  m.elapsedMs += dtMs;
  while (m.elapsedMs >= m.runMs) {
    m.elapsedMs -= m.runMs;
    m.dir = (m.dir == FWD) ? BWD : FWD;
  }
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(M1_IN1, OUTPUT); pinMode(M1_IN2, OUTPUT); pinMode(M1_IN3, OUTPUT); pinMode(M1_IN4, OUTPUT);
  pinMode(M2_IN1, OUTPUT); pinMode(M2_IN2, OUTPUT); pinMode(M2_IN3, OUTPUT); pinMode(M2_IN4, OUTPUT);
  pinMode(M3_IN1, OUTPUT); pinMode(M3_IN2, OUTPUT); pinMode(M3_IN3, OUTPUT); pinMode(M3_IN4, OUTPUT);
  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(IR3_PIN, INPUT);

  release(motor1); release(motor2); release(motor3);
  startupMs = millis();
  lastUpdateMs = startupMs;

  if (RUN_BOTH_M1_M2) {
    Serial.println(F("TEST MODE: Motor 1 + Motor 2 together"));
    Serial.println(F("Both run 10s FWD / 10s BWD"));
    if (DUAL_TEST_DISABLE_IR_STOP) {
      Serial.println(F("Dual test: IR stop disabled"));
    }
  } else {
    Serial.print(F("TEST MODE: Motor "));
    Serial.print(TEST_MOTOR);
    Serial.println(F(" only"));
    Serial.println(F("10s FWD / 10s BWD until IR detects"));
  }
  if (COIL_CHASE_MODE) {
    Serial.println(F("COIL CHASE MODE: energizing one coil at a time"));
    if (COIL_CHASE_BOTH_M1_M2) {
      Serial.println(F("ULN TEST: Motor 1 and Motor 2 together"));
    }
  }
}

void loop() {
  unsigned long now = millis();
  unsigned long dt = now - lastUpdateMs;
  lastUpdateMs = now;

  int raw1 = digitalRead(IR1_PIN);
  int raw2 = digitalRead(IR2_PIN);
  int raw3 = digitalRead(IR3_PIN);
  bool det = RUN_BOTH_M1_M2 ? (isDetected(raw1) || isDetected(raw2)) :
             (TEST_MOTOR == 1) ? isDetected(raw1) :
             (TEST_MOTOR == 2) ? isDetected(raw2) : isDetected(raw3);

  if (!stopped) {
    bool inGrace = (now - startupMs) < STARTUP_GRACE_MS;
    bool ignoreStop = DEBUG_MODE || (RUN_BOTH_M1_M2 && DUAL_TEST_DISABLE_IR_STOP);
    if (inGrace || ignoreStop) {
      detectAccumMs = 0;
    } else if (det) {
      detectAccumMs += dt;
      if (detectAccumMs >= DETECT_HOLD_MS) {
        stopped = true;
        Serial.println(F("*** IR DETECTED - STOPPED ***"));
      }
    } else {
      detectAccumMs = 0;
    }
  }

  if (stopped) {
    release(motor1); release(motor2); release(motor3);
  } else {
    Motor& m = (TEST_MOTOR == 1) ? motor1 : (TEST_MOTOR == 2) ? motor2 : motor3;
    if (COIL_CHASE_MODE) {
      if (now - lastCoilStepMs >= COIL_HOLD_MS) {
        coilIndex = (coilIndex + 1) & 0x03;
        if (COIL_CHASE_BOTH_M1_M2) {
          if (coilIndex == 0) {
            writeCoils(motor1, 1, 0, 0, 0);
            writeCoils(motor2, 1, 0, 0, 0);
          } else if (coilIndex == 1) {
            writeCoils(motor1, 0, 1, 0, 0);
            writeCoils(motor2, 0, 1, 0, 0);
          } else if (coilIndex == 2) {
            writeCoils(motor1, 0, 0, 1, 0);
            writeCoils(motor2, 0, 0, 1, 0);
          } else {
            writeCoils(motor1, 0, 0, 0, 1);
            writeCoils(motor2, 0, 0, 0, 1);
          }
          release(motor3);
        } else if (coilIndex == 0) {
          writeCoils(m, 1, 0, 0, 0);
        } else if (coilIndex == 1) {
          writeCoils(m, 0, 1, 0, 0);
        } else if (coilIndex == 2) {
          writeCoils(m, 0, 0, 1, 0);
        } else {
          writeCoils(m, 0, 0, 0, 1);
        }
        lastCoilStepMs = now;
      }
    } else {
      if (RUN_BOTH_M1_M2) {
        updateDir(motor1, dt);
        updateDir(motor2, dt);
        release(motor3);

        unsigned long nowUs = micros();
        if (nowUs - lastStepM1Us >= STEP_INTERVAL_M1_US) {
          (motor1.dir == FWD) ? stepFwd(motor1) : stepBwd(motor1);
          lastStepM1Us = nowUs;
        }
        if (nowUs - lastStepM2Us >= STEP_INTERVAL_M2_US) {
          (motor2.dir == FWD) ? stepFwd(motor2) : stepBwd(motor2);
          lastStepM2Us = nowUs;
        }
      } else {
        updateDir(m, dt);

        unsigned long nowUs = micros();
        if (nowUs - lastStepUs >= STEP_INTERVAL_M1_US) {
          (m.dir == FWD) ? stepFwd(m) : stepBwd(m);
          lastStepUs = nowUs;
        }
      }
    }
  }

  if (now - lastPrintMs >= IR_PRINT_INTERVAL_MS) {
    lastPrintMs = now;
    Motor& m = (TEST_MOTOR == 1) ? motor1 : (TEST_MOTOR == 2) ? motor2 : motor3;
    Serial.print(F("IR1=")); Serial.print(raw1);
    Serial.print(F(" IR2=")); Serial.print(raw2);
    Serial.print(F(" IR3=")); Serial.print(raw3);
    if (COIL_CHASE_MODE && COIL_CHASE_BOTH_M1_M2) {
      Serial.print(F(" M1Pins:"));
      Serial.print(digitalRead(motor1.in1)); Serial.print(digitalRead(motor1.in2));
      Serial.print(digitalRead(motor1.in3)); Serial.print(digitalRead(motor1.in4));
      Serial.print(F(" M2Pins:"));
      Serial.print(digitalRead(motor2.in1)); Serial.print(digitalRead(motor2.in2));
      Serial.print(digitalRead(motor2.in3)); Serial.print(digitalRead(motor2.in4));
    } else if (RUN_BOTH_M1_M2) {
      Serial.print(F(" M1="));
      Serial.print(motor1.dir == FWD ? F("FWD") : F("BWD"));
      Serial.print(F(" M2="));
      Serial.print(motor2.dir == FWD ? F("FWD") : F("BWD"));
    } else {
      Serial.print(F(" M")); Serial.print(TEST_MOTOR); Serial.print(F("="));
      Serial.print(F(" Pins:"));
      Serial.print(digitalRead(m.in1)); Serial.print(digitalRead(m.in2));
      Serial.print(digitalRead(m.in3)); Serial.print(digitalRead(m.in4));
    }
    if (stopped) {
      Serial.println(F("HALTED"));
    } else {
      if (COIL_CHASE_MODE) {
        Serial.print(F("COIL="));
        Serial.println(coilIndex + 1);
      } else if (RUN_BOTH_M1_M2) {
        Serial.println();
      } else {
        Serial.println(m.dir == FWD ? F("FWD") : F("BWD"));
      }
    }
  }
}
