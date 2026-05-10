#include <AccelStepper.h>
#include <SoftwareSerial.h>

// SoftwareSerial for ESP32 communication (A4=RX, A5=TX)
// Uses analog pins 18 and 19 when configured as digital
SoftwareSerial espSerial(18, 19);  // RX on A4 (pin 18), TX on A5 (pin 19)

// UNO firmware compatible with ESP32 cash controller protocol.
// Accepted commands:
//   PING
//   STATUS
//   STOP
//   DISPENSE <motor 1..3> <count>
//
// Expected responses consumed by ESP32:
//   PONG
//   STATUS ...
//   OK DISPENSE motor=<n> count=<c>
//   DONE motor=<n> count=<c>
//   ERR motor=<n> reason=<text>
//   OK STOP

namespace {

constexpr long SERIAL_BAUD = 9600;
constexpr float MOTOR1_RUN_SPEED_STEPS_PER_SEC = 1200.0f;
constexpr float DEFAULT_RUN_SPEED_STEPS_PER_SEC = 1400.0f;
constexpr float MOTOR1_ACCEL_STEPS_PER_SEC2 = 500.0f;
constexpr float DEFAULT_ACCEL_STEPS_PER_SEC2 = 650.0f;
constexpr unsigned long MOTOR1_FORWARD_RUN_MS = 14000;
constexpr unsigned long MOTOR1_BACKWARD_RUN_MS = 14000;
constexpr unsigned long DEFAULT_FORWARD_RUN_MS = 10000;
constexpr unsigned long DEFAULT_BACKWARD_RUN_MS = 10000;
constexpr unsigned long JOB_MAX_RUNTIME_MS = 900000; // 15 minutes safety cap

// IR mapping provided by user
constexpr uint8_t IR_1_PESO_PIN = A0;
constexpr uint8_t IR_5_PESO_PIN = A1;
constexpr uint8_t IR_10_PESO_PIN = A2;
constexpr uint8_t IR_20_PESO_PIN = A3;

constexpr bool IR_ACTIVE_LOW = true;

// 3 remote motors on Uno (motor 4 is local on ESP32 firmware).
AccelStepper motor1(AccelStepper::HALF4WIRE, 2, 4, 3, 5);     // 1 peso
AccelStepper motor2(AccelStepper::HALF4WIRE, 6, 8, 7, 9);     // 5 peso
AccelStepper motor3(AccelStepper::HALF4WIRE, 10, 12, 11, 13); // 10 peso

struct MotorSlot {
  uint8_t motorNumber;
  int denomination;
  uint8_t irPin;
  AccelStepper* stepper;
  float forwardSpeedStepsPerSec;
  float backwardSpeedStepsPerSec;
  unsigned long forwardRunMs;
  unsigned long backwardRunMs;
};

MotorSlot slots[] = {
  {1, 1, IR_1_PESO_PIN, &motor1, MOTOR1_RUN_SPEED_STEPS_PER_SEC, MOTOR1_RUN_SPEED_STEPS_PER_SEC, MOTOR1_FORWARD_RUN_MS, MOTOR1_BACKWARD_RUN_MS},
  {2, 5, IR_5_PESO_PIN, &motor2, DEFAULT_RUN_SPEED_STEPS_PER_SEC, DEFAULT_RUN_SPEED_STEPS_PER_SEC, DEFAULT_FORWARD_RUN_MS, DEFAULT_BACKWARD_RUN_MS},
  {3, 10, IR_10_PESO_PIN, &motor3, DEFAULT_RUN_SPEED_STEPS_PER_SEC, DEFAULT_RUN_SPEED_STEPS_PER_SEC, DEFAULT_FORWARD_RUN_MS, DEFAULT_BACKWARD_RUN_MS},
};

constexpr size_t SLOT_COUNT = sizeof(slots) / sizeof(slots[0]);

enum JobPhase : uint8_t {
  JOB_IDLE = 0,
  JOB_FORWARD = 1,
  JOB_BACKWARD = 2,
};

struct DispenseJob {
  bool active;
  uint8_t motorNumber;
  uint8_t targetCount;
  uint8_t dispensedCount;
  JobPhase phase;
  unsigned long phaseStartedMs;
  unsigned long cycleStartedMs;
  bool cycleDetected;
  bool lastBlockedState;
  unsigned long deadlineMs;
};

DispenseJob job = {};
String serialBuffer;
bool irLastState[4] = {false, false, false, false};
bool frameSync = false;

uint8_t irPinByIndex(uint8_t index) {
  switch (index) {
    case 0: return IR_1_PESO_PIN;
    case 1: return IR_5_PESO_PIN;
    case 2: return IR_10_PESO_PIN;
    default: return IR_20_PESO_PIN;
  }
}

int irDenomByIndex(uint8_t index) {
  switch (index) {
    case 0: return 1;
    case 1: return 5;
    case 2: return 10;
    default: return 20;
  }
}

bool readIrBlocked(uint8_t pin) {
  const int raw = digitalRead(pin);
  return IR_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
}

MotorSlot* findSlot(uint8_t motorNumber) {
  for (size_t index = 0; index < SLOT_COUNT; ++index) {
    if (slots[index].motorNumber == motorNumber) {
      return &slots[index];
    }
  }
  return nullptr;
}

void stopAllSteppers() {
  for (size_t index = 0; index < SLOT_COUNT; ++index) {
    slots[index].stepper->stop();
    slots[index].stepper->disableOutputs();
  }
}

void printStatus() {
  espSerial.print(F("STATUS busy="));
  espSerial.print(job.active ? 1 : 0);
  espSerial.print(F(" m="));
  espSerial.print(job.motorNumber);
  espSerial.print(F(" c="));
  espSerial.print(job.dispensedCount);
  espSerial.print('/');
  espSerial.print(job.targetCount);
  espSerial.print(F(" phase="));
  espSerial.print(static_cast<uint8_t>(job.phase));
  espSerial.print(F(" detected="));
  espSerial.print(job.cycleDetected ? 1 : 0);
  espSerial.print(F(" ir1="));
  espSerial.print(readIrBlocked(IR_1_PESO_PIN) ? 1 : 0);
  espSerial.print(F(" ir5="));
  espSerial.print(readIrBlocked(IR_5_PESO_PIN) ? 1 : 0);
  espSerial.print(F(" ir10="));
  espSerial.print(readIrBlocked(IR_10_PESO_PIN) ? 1 : 0);
  espSerial.print(F(" ir20="));
  espSerial.println(readIrBlocked(IR_20_PESO_PIN) ? 1 : 0);

  for (size_t index = 0; index < SLOT_COUNT; ++index) {
    espSerial.print(F("STATUS motor="));
    espSerial.print(slots[index].motorNumber);
    espSerial.print(F(" fwdSpeed="));
    espSerial.print(slots[index].forwardSpeedStepsPerSec);
    espSerial.print(F(" backSpeed="));
    espSerial.println(slots[index].backwardSpeedStepsPerSec);
  }
}

void finishJobDone() {
  espSerial.print(F("DONE motor="));
  espSerial.print(job.motorNumber);
  espSerial.print(F(" count="));
  espSerial.println(job.dispensedCount);
  job = {};
}

void failJob(const __FlashStringHelper* reason) {
  espSerial.print(F("ERR motor="));
  espSerial.print(job.motorNumber);
  espSerial.print(F(" reason="));
  espSerial.println(reason);
  job = {};
}

void startDispense(uint8_t motorNumber, uint8_t count) {
  MotorSlot* slot = findSlot(motorNumber);
  if (slot == nullptr) {
    espSerial.print(F("ERR motor="));
    espSerial.print(motorNumber);
    espSerial.println(F(" reason=unsupported"));
    return;
  }
  if (count == 0) {
    espSerial.print(F("ERR motor="));
    espSerial.print(motorNumber);
    espSerial.println(F(" reason=invalid-count"));
    return;
  }
  if (job.active) {
    espSerial.print(F("ERR motor="));
    espSerial.print(motorNumber);
    espSerial.println(F(" reason=busy"));
    return;
  }

  stopAllSteppers();
  slot->stepper->enableOutputs();
  const float forwardSpeed = slot->forwardSpeedStepsPerSec >= 0 ? slot->forwardSpeedStepsPerSec : -slot->forwardSpeedStepsPerSec;
  const float accel = (slot->motorNumber == 1) ? MOTOR1_ACCEL_STEPS_PER_SEC2 : DEFAULT_ACCEL_STEPS_PER_SEC2;
  slot->stepper->setMaxSpeed(forwardSpeed);
  slot->stepper->setAcceleration(accel);
  slot->stepper->moveTo(2000000L);

  job.active = true;
  job.motorNumber = motorNumber;
  job.targetCount = count;
  job.dispensedCount = 0;
  const unsigned long now = millis();
  job.phase = JOB_FORWARD;
  job.phaseStartedMs = now;
  job.cycleStartedMs = now;
  job.cycleDetected = false;
  job.lastBlockedState = readIrBlocked(slot->irPin);
  job.deadlineMs = now + JOB_MAX_RUNTIME_MS;

  espSerial.print(F("OK DISPENSE motor="));
  espSerial.print(motorNumber);
  espSerial.print(F(" count="));
  espSerial.println(count);
}

void handleDispenseJob() {
  if (!job.active) {
    return;
  }

  MotorSlot* slot = findSlot(job.motorNumber);
  if (slot == nullptr) {
    failJob(F("invalid-slot"));
    stopAllSteppers();
    return;
  }

  slot->stepper->run();

  const unsigned long now = millis();
  if (now > job.deadlineMs) {
    stopAllSteppers();
    failJob(F("timeout"));
    return;
  }

  const float forwardSpeed = slot->forwardSpeedStepsPerSec >= 0 ? slot->forwardSpeedStepsPerSec : -slot->forwardSpeedStepsPerSec;
  const float backwardSpeed = slot->backwardSpeedStepsPerSec >= 0 ? slot->backwardSpeedStepsPerSec : -slot->backwardSpeedStepsPerSec;
  const float accel = (slot->motorNumber == 1) ? MOTOR1_ACCEL_STEPS_PER_SEC2 : DEFAULT_ACCEL_STEPS_PER_SEC2;
  const bool blocked = readIrBlocked(slot->irPin);

  if (!job.cycleDetected && blocked && !job.lastBlockedState) {
    job.cycleDetected = true;
    espSerial.print(F("HIT motor="));
    espSerial.print(job.motorNumber);
    espSerial.print(F(" t="));
    espSerial.println(now - job.cycleStartedMs);
  }

  job.lastBlockedState = blocked;

  if (job.phase == JOB_FORWARD) {
    if (now - job.phaseStartedMs >= slot->forwardRunMs) {
      espSerial.print(F("PHASE motor="));
      espSerial.print(job.motorNumber);
      espSerial.print(F(" forward_ms="));
      espSerial.println(now - job.phaseStartedMs);
      slot->stepper->setMaxSpeed(backwardSpeed);
      slot->stepper->setAcceleration(accel);
      slot->stepper->moveTo(-2000000L);
      job.phase = JOB_BACKWARD;
      job.phaseStartedMs = now;
    }
    return;
  }

  if (job.phase == JOB_BACKWARD) {
    if (now - job.phaseStartedMs >= slot->backwardRunMs) {
      espSerial.print(F("PHASE motor="));
      espSerial.print(job.motorNumber);
      espSerial.print(F(" backward_ms="));
      espSerial.println(now - job.phaseStartedMs);
      if (job.cycleDetected) {
        ++job.dispensedCount;
        if (job.dispensedCount >= job.targetCount) {
          stopAllSteppers();
          finishJobDone();
          return;
        }
      }

      slot->stepper->setMaxSpeed(forwardSpeed);
      slot->stepper->setAcceleration(accel);
    slot->stepper->moveTo(2000000L);
      job.phase = JOB_FORWARD;
      job.phaseStartedMs = now;
      job.cycleStartedMs = now;
      job.cycleDetected = false;
      job.lastBlockedState = blocked;
    }
    return;
  }
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  // Sanitize incoming text: keep only letters/digits/spaces so noisy bytes don't break parsing.
  String clean;
  clean.reserve(line.length());
  for (int index = 0; index < line.length(); ++index) {
    char c = line[index];
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - ('a' - 'A'));
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ') {
      clean += c;
      continue;
    }
    if (c == '\t') {
      clean += ' ';
    }
  }
  clean.trim();
  if (clean.length() == 0) {
    return;
  }

  line = clean;

  if (line == F("PING") || line == F("PINGUNO") || line.startsWith(F("PING ")) || line.endsWith(F(" PING")) || line.indexOf(F(" PING ")) >= 0) {
    espSerial.println(F("PONG"));
    return;
  }

  if (line == F("STATUS")) {
    printStatus();
    return;
  }

  if (line == F("STOP")) {
    stopAllSteppers();
    job = {};
    espSerial.println(F("OK STOP"));
    return;
  }

  if (line == F("HELP")) {
    espSerial.println(F("Commands: PING, STATUS, STOP, DISPENSE <motor 1..3> <count>"));
    return;
  }

  int motorNumber = 0;
  int count = 0;
  const bool hasDispenseToken = line.startsWith(F("DISPENSE")) || line.startsWith(F("DISP")) || line.indexOf(F(" DISPENSE")) >= 0 || line.indexOf(F(" DISP")) >= 0;
  if (hasDispenseToken && sscanf(line.c_str(), "%*[^0-9]%d %d", &motorNumber, &count) == 2) {
    startDispense(static_cast<uint8_t>(motorNumber), static_cast<uint8_t>(count));
    return;
  }

  espSerial.print(F("ERR motor=0 reason=unknown-command cmd="));
  espSerial.println(line);
}

void readSerialLines() {
  // Read from ESP32 via SoftwareSerial
  while (espSerial.available() > 0) {
    const char incoming = static_cast<char>(espSerial.read());
    if (incoming == '@') {
      serialBuffer = "";
      frameSync = true;
      continue;
    }
    if (!frameSync) {
      continue;
    }
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      handleCommand(serialBuffer);
      serialBuffer = "";
      frameSync = false;
      continue;
    }
    if (serialBuffer.length() < 80) {
      serialBuffer += incoming;
    }
  }
}

void pollIrEdgeLogs() {
  for (uint8_t index = 0; index < 4; ++index) {
    const bool blocked = readIrBlocked(irPinByIndex(index));
    if (blocked != irLastState[index]) {
      irLastState[index] = blocked;
      espSerial.print(F("IR "));
      espSerial.print(irDenomByIndex(index));
      espSerial.print(F("P "));
      espSerial.println(blocked ? F("BLOCK") : F("CLEAR"));
    }
  }
}

void setupMotor(AccelStepper& motor) {
  motor.setMaxSpeed(DEFAULT_RUN_SPEED_STEPS_PER_SEC);
  motor.setAcceleration(DEFAULT_ACCEL_STEPS_PER_SEC2);
  motor.disableOutputs();
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);
  espSerial.begin(SERIAL_BAUD);   // ESP32 serial on A4/A5

  pinMode(IR_1_PESO_PIN, INPUT_PULLUP);
  pinMode(IR_5_PESO_PIN, INPUT_PULLUP);
  pinMode(IR_10_PESO_PIN, INPUT_PULLUP);
  pinMode(IR_20_PESO_PIN, INPUT_PULLUP);

  setupMotor(motor1);
  setupMotor(motor2);
  setupMotor(motor3);

  for (uint8_t index = 0; index < 4; ++index) {
    irLastState[index] = readIrBlocked(irPinByIndex(index));
  }

  Serial.println(F("UNO coin dispenser ready on USB Serial"));
  espSerial.println(F("UNO READY"));
}

void loop() {
  readSerialLines();
  pollIrEdgeLogs();
  handleDispenseJob();
}