#include <AccelStepper.h>
#include <SoftwareSerial.h>

// SoftwareSerial for ESP32 communication (A0=RX, A5=TX)
// RX moved off A3 (also unreliable) onto A0 (digital pin 14)
SoftwareSerial espSerial(14, 19);  // RX on A0 (pin 14), TX on A5 (pin 19)

// Tee writes to both espSerial AND USB Serial so replies reach the ESP32
// either via the direct hardware wire OR via the PC USB-relay bridge.
class TeeStream : public Print {
 public:
  size_t write(uint8_t b) override {
    espSerial.write(b);
    return Serial.write(b);
  }
  size_t write(const uint8_t *buf, size_t n) override {
    espSerial.write(buf, n);
    return Serial.write(buf, n);
  }
};
TeeStream replyOut;

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
constexpr unsigned long MOTOR1_BACKWARD_RUN_MS = 2000;
constexpr unsigned long DEFAULT_FORWARD_RUN_MS = 10000;
constexpr unsigned long DEFAULT_BACKWARD_RUN_MS = 2000;
constexpr unsigned long JOB_MAX_RUNTIME_MS = 900000; // 15 minutes safety cap

// IR mapping provided by user
constexpr uint8_t IR_1_PESO_PIN = A3;   // moved from A0 to free A0 for SoftwareSerial RX
constexpr uint8_t IR_5_PESO_PIN = A1;
constexpr uint8_t IR_10_PESO_PIN = A4;  // moved from A2 to A4 (rewired hardware)
constexpr uint8_t IR_20_PESO_PIN = A2;  // 20P slot not used on Uno; pin reassigned

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
  // Motor 3 dropped to motor1's slower profile (1200 sps) to recover from stall symptoms (all coils energizing but rotor not turning).
  {3, 10, IR_10_PESO_PIN, &motor3, MOTOR1_RUN_SPEED_STEPS_PER_SEC, MOTOR1_RUN_SPEED_STEPS_PER_SEC, MOTOR1_FORWARD_RUN_MS, MOTOR1_BACKWARD_RUN_MS},
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
String usbBuffer;
bool usbRxMonitor = false;
unsigned long usbRxMonitorUntilMs = 0;
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
  replyOut.print(F("STATUS busy="));
  replyOut.print(job.active ? 1 : 0);
  replyOut.print(F(" m="));
  replyOut.print(job.motorNumber);
  replyOut.print(F(" c="));
  replyOut.print(job.dispensedCount);
  replyOut.print('/');
  replyOut.print(job.targetCount);
  replyOut.print(F(" phase="));
  replyOut.print(static_cast<uint8_t>(job.phase));
  replyOut.print(F(" detected="));
  replyOut.print(job.cycleDetected ? 1 : 0);
  replyOut.print(F(" ir1="));
  replyOut.print(readIrBlocked(IR_1_PESO_PIN) ? 1 : 0);
  replyOut.print(F(" ir5="));
  replyOut.print(readIrBlocked(IR_5_PESO_PIN) ? 1 : 0);
  replyOut.print(F(" ir10="));
  replyOut.print(readIrBlocked(IR_10_PESO_PIN) ? 1 : 0);
  replyOut.print(F(" ir20="));
  replyOut.println(readIrBlocked(IR_20_PESO_PIN) ? 1 : 0);

  for (size_t index = 0; index < SLOT_COUNT; ++index) {
    replyOut.print(F("STATUS motor="));
    replyOut.print(slots[index].motorNumber);
    replyOut.print(F(" fwdSpeed="));
    replyOut.print(slots[index].forwardSpeedStepsPerSec);
    replyOut.print(F(" backSpeed="));
    replyOut.println(slots[index].backwardSpeedStepsPerSec);
  }
}

void finishJobDone() {
  replyOut.print(F("DONE motor="));
  replyOut.print(job.motorNumber);
  replyOut.print(F(" count="));
  replyOut.println(job.dispensedCount);
  job = {};
}

void failJob(const __FlashStringHelper* reason) {
  replyOut.print(F("ERR motor="));
  replyOut.print(job.motorNumber);
  replyOut.print(F(" reason="));
  replyOut.println(reason);
  job = {};
}

void startDispense(uint8_t motorNumber, uint8_t count) {
  MotorSlot* slot = findSlot(motorNumber);
  if (slot == nullptr) {
    replyOut.print(F("ERR motor="));
    replyOut.print(motorNumber);
    replyOut.println(F(" reason=unsupported"));
    return;
  }
  if (count == 0) {
    replyOut.print(F("ERR motor="));
    replyOut.print(motorNumber);
    replyOut.println(F(" reason=invalid-count"));
    return;
  }
  if (job.active) {
    replyOut.print(F("ERR motor="));
    replyOut.print(motorNumber);
    replyOut.println(F(" reason=busy"));
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

  replyOut.print(F("OK DISPENSE motor="));
  replyOut.print(motorNumber);
  replyOut.print(F(" count="));
  replyOut.println(count);
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
    replyOut.print(F("HIT motor="));
    replyOut.print(job.motorNumber);
    replyOut.print(F(" t="));
    replyOut.println(now - job.cycleStartedMs);
  }

  job.lastBlockedState = blocked;

  if (job.phase == JOB_FORWARD) {
    // End forward phase as soon as the coin is detected (HIT), but only after
    // a 500ms minimum so startup IR noise can't trigger an instant exit.
    // Also still respect the maximum forwardRunMs as a safety timeout.
    const unsigned long forwardElapsed = now - job.phaseStartedMs;
    const bool earlyExitOnHit = job.cycleDetected && forwardElapsed >= 500;
    if (earlyExitOnHit || forwardElapsed >= slot->forwardRunMs) {
      replyOut.print(F("PHASE motor="));
      replyOut.print(job.motorNumber);
      replyOut.print(F(" forward_ms="));
      replyOut.println(forwardElapsed);
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
      replyOut.print(F("PHASE motor="));
      replyOut.print(job.motorNumber);
      replyOut.print(F(" backward_ms="));
      replyOut.println(now - job.phaseStartedMs);
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
    replyOut.println(F("PONG"));
    return;
  }

  if (line == F("STATUS")) {
    printStatus();
    return;
  }

  if (line == F("STOP")) {
    stopAllSteppers();
    job = {};
    replyOut.println(F("OK STOP"));
    return;
  }

  if (line == F("HELP")) {
    replyOut.println(F("Commands: PING, STATUS, STOP, DISPENSE <motor 1..3> <count>"));
    return;
  }

  int motorNumber = 0;
  int count = 0;
  const bool hasDispenseToken = line.startsWith(F("DISPENSE")) || line.startsWith(F("DISP")) || line.indexOf(F(" DISPENSE")) >= 0 || line.indexOf(F(" DISP")) >= 0;
  if (hasDispenseToken && sscanf(line.c_str(), "%*[^0-9]%d %d", &motorNumber, &count) == 2) {
    startDispense(static_cast<uint8_t>(motorNumber), static_cast<uint8_t>(count));
    return;
  }

  replyOut.print(F("ERR motor=0 reason=unknown-command cmd="));
  replyOut.println(line);
}

void readSerialLines() {
  // Read from ESP32 via SoftwareSerial
  while (espSerial.available() > 0) {
    const char incoming = static_cast<char>(espSerial.read());
    if (usbRxMonitor) {
      // Log every received byte to USB so we can see what's coming over the wire.
      Serial.print(F("RX 0x"));
      if ((uint8_t)incoming < 16) Serial.print('0');
      Serial.print((uint8_t)incoming, HEX);
      Serial.print(F(" '"));
      if (incoming >= 32 && incoming < 127) Serial.print(incoming); else Serial.print('.');
      Serial.println('\'');
    }
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
      replyOut.print(F("IR "));
      replyOut.print(irDenomByIndex(index));
      replyOut.print(F("P "));
      replyOut.println(blocked ? F("BLOCK") : F("CLEAR"));
    }
  }
}

// USB diagnostics: lets you talk to the Uno over its native USB Serial
// (Arduino IDE Serial Monitor at 9600) WITHOUT involving the ESP32 wire.
// Commands (terminate with newline):
//   PING        -> replies PONG_USB on USB. Confirms Uno is alive.
//   TXTEST      -> sends "@PING\nUNO_TXTEST\n" out espSerial 5 times.
//                  ESP32 should print these as `UNO> ...` lines.
//                  Tests Uno-A5 -> ESP32-GPIO16 wire.
//   RXMON       -> for the next 5 seconds, dump every byte received on
//                  espSerial to USB as hex+ascii. Then on ESP32 type
//                  PINGUNO; if any RX appears, ESP32-GPIO17 -> Uno-A4
//                  wire works. If nothing appears, that wire is broken.
//   STATUS / DISPENSE m c / STOP -> same as the espSerial protocol but
//                  reply goes to USB instead of espSerial.
void readUsbDiag() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      String line = usbBuffer;
      usbBuffer = "";
      line.trim();
      if (line.length() == 0) continue;
      String upper = line;
      upper.toUpperCase();
      // Bridge protocol: a leading '@' means this command was relayed from
      // the ESP32 over the PC USB bridge instead of arriving on espSerial.
      // Route it through the normal handler; replies go via replyOut (USB+espSerial).
      if (line.length() > 0 && line[0] == '@') {
        String inner = line.substring(1);
        inner.trim();
        if (inner.length() > 0) handleCommand(inner);
        continue;
      }
      if (upper == F("PING")) {
        Serial.println(F("PONG_USB"));
        continue;
      }
      if (upper == F("TXTEST")) {
        Serial.println(F("DIAG TXTEST sending 5x to espSerial"));
        for (uint8_t i = 0; i < 5; ++i) {
          replyOut.println(F("UNO_TXTEST"));
          delay(150);
        }
        Serial.println(F("DIAG TXTEST done"));
        continue;
      }
      if (upper == F("RXMON")) {
        usbRxMonitor = true;
        usbRxMonitorUntilMs = millis() + 30000UL;
        Serial.println(F("DIAG rx-monitor on for 30s; trigger ESP32->Uno traffic now"));
        continue;
      }
      if (upper == F("STATUS")) {
        Serial.print(F("USB_STATUS busy="));
        Serial.print(job.active ? 1 : 0);
        Serial.print(F(" m="));
        Serial.print(job.motorNumber);
        Serial.print(F(" c="));
        Serial.print(job.dispensedCount);
        Serial.print('/');
        Serial.println(job.targetCount);
        continue;
      }
      if (upper == F("STOP")) {
        stopAllSteppers();
        job = {};
        Serial.println(F("USB OK STOP"));
        continue;
      }
      int motorNumber = 0;
      int count = 0;
      if (upper.startsWith(F("DISPENSE")) || upper.startsWith(F("DISP"))) {
        if (sscanf(line.c_str(), "%*[^0-9]%d %d", &motorNumber, &count) == 2) {
          Serial.print(F("USB DISPENSE motor="));
          Serial.print(motorNumber);
          Serial.print(F(" count="));
          Serial.println(count);
          startDispense((uint8_t)motorNumber, (uint8_t)count);
          continue;
        }
      }
      Serial.print(F("USB unknown cmd: "));
      Serial.println(line);
      continue;
    }
    if (usbBuffer.length() < 80) usbBuffer += c;
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
  replyOut.println(F("UNO READY"));
}

void loop() {
  readSerialLines();
  readUsbDiag();
  pollIrEdgeLogs();
  handleDispenseJob();
  if (usbRxMonitor && (long)(millis() - usbRxMonitorUntilMs) >= 0) {
    usbRxMonitor = false;
    Serial.println(F("DIAG rx-monitor stopped"));
  }
}