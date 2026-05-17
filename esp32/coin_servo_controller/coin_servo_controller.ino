#include <ESP32Servo.h>

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t MOTOR_COUNT = 4;
constexpr uint8_t INVALID_PIN = 255;

struct MotorConfig {
  uint8_t servoPin;
  uint8_t irPin;
  bool irActiveLow;
  int forwardUs;
  int reverseUs;
  int stopUs;
  unsigned long startupSettleMs;
  unsigned long timeoutMs;
  uint8_t denomination;
};

// TODO: Fill these with your real pin map for ESP32 #2.
MotorConfig MOTOR_CFG[MOTOR_COUNT] = {
  {INVALID_PIN, INVALID_PIN, true, 1700, 1300, 1500, 300, 12000, 1},
  {INVALID_PIN, INVALID_PIN, true, 1700, 1300, 1500, 300, 12000, 5},
  {INVALID_PIN, INVALID_PIN, true, 1700, 1300, 1500, 300, 12000, 10},
  {INVALID_PIN, INVALID_PIN, true, 1700, 1300, 1500, 300, 12000, 20},
};

struct DispenseJob {
  bool active;
  uint8_t motor;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  unsigned long startedMs;
  unsigned long settleUntilMs;
};

Servo motorServos[MOTOR_COUNT];
bool servoAttached[MOTOR_COUNT] = {false, false, false, false};
DispenseJob currentJob = {};
String commandBuffer;

bool isMotorConfigured(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return false;
  return MOTOR_CFG[motorIndex].servoPin != INVALID_PIN && MOTOR_CFG[motorIndex].irPin != INVALID_PIN;
}

void attachMotorServo(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return;
  if (servoAttached[motorIndex]) return;
  motorServos[motorIndex].setPeriodHertz(50);
  motorServos[motorIndex].attach(MOTOR_CFG[motorIndex].servoPin, 500, 2500);
  servoAttached[motorIndex] = true;
}

void stopMotor(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return;
  if (!servoAttached[motorIndex]) return;
  motorServos[motorIndex].writeMicroseconds(MOTOR_CFG[motorIndex].stopUs);
}

bool isIrDetected(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT || !isMotorConfigured(motorIndex)) return false;
  const int raw = digitalRead(MOTOR_CFG[motorIndex].irPin);
  return MOTOR_CFG[motorIndex].irActiveLow ? (raw == LOW) : (raw == HIGH);
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  PING"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  DISPENSE <motor 1..4> <count>"));
  Serial.println(F("  STOP"));
  Serial.println(F("  HELP"));
}

void printStatus() {
  Serial.print(F("STATUS active="));
  Serial.print(currentJob.active ? F("1") : F("0"));
  if (currentJob.active) {
    Serial.print(F(" motor="));
    Serial.print(currentJob.motor);
    Serial.print(F(" done="));
    Serial.print(currentJob.dispensedCount);
    Serial.print(F("/"));
    Serial.print(currentJob.requestedCount);
  }
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    Serial.print(F(" ir"));
    Serial.print(i + 1);
    Serial.print('=');
    if (isMotorConfigured(i)) {
      Serial.print(digitalRead(MOTOR_CFG[i].irPin));
    } else {
      Serial.print(F("NA"));
    }
  }
  Serial.println();
}

void finishJob() {
  const uint8_t motorIndex = currentJob.motor - 1;
  const uint8_t dispensed = currentJob.dispensedCount;
  stopMotor(motorIndex);
  currentJob = {};
  Serial.print(F("DONE motor="));
  Serial.print(motorIndex + 1);
  Serial.print(F(" count="));
  Serial.print(dispensed);
  Serial.print(F(" amount="));
  Serial.println(dispensed * MOTOR_CFG[motorIndex].denomination);
}

void failJob(const __FlashStringHelper* reason) {
  const uint8_t motor = currentJob.motor;
  if (motor >= 1 && motor <= MOTOR_COUNT) {
    stopMotor(motor - 1);
  }
  currentJob = {};
  Serial.print(F("ERR motor="));
  Serial.print(motor);
  Serial.print(F(" reason="));
  Serial.println(reason);
}

void startDispense(uint8_t motorNumber, uint8_t count) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT || count == 0) {
    Serial.println(F("ERR invalid-args"));
    return;
  }
  if (currentJob.active) {
    Serial.println(F("ERR busy"));
    return;
  }

  const uint8_t motorIndex = motorNumber - 1;
  if (!isMotorConfigured(motorIndex)) {
    Serial.print(F("ERR motor="));
    Serial.print(motorNumber);
    Serial.println(F(" reason=not-configured"));
    return;
  }

  attachMotorServo(motorIndex);
  motorServos[motorIndex].writeMicroseconds(MOTOR_CFG[motorIndex].forwardUs);

  currentJob.active = true;
  currentJob.motor = motorNumber;
  currentJob.requestedCount = count;
  currentJob.dispensedCount = 0;
  currentJob.sensorArmed = !isIrDetected(motorIndex);
  currentJob.startedMs = millis();
  currentJob.settleUntilMs = currentJob.startedMs + MOTOR_CFG[motorIndex].startupSettleMs;

  Serial.print(F("OK DISPENSE motor="));
  Serial.print(motorNumber);
  Serial.print(F(" count="));
  Serial.println(count);
}

void serviceDispense() {
  if (!currentJob.active) return;

  const uint8_t motorIndex = currentJob.motor - 1;
  const unsigned long nowMs = millis();

  if (nowMs - currentJob.startedMs >= MOTOR_CFG[motorIndex].timeoutMs) {
    failJob(F("timeout"));
    return;
  }

  if (nowMs < currentJob.settleUntilMs) return;

  const bool detected = isIrDetected(motorIndex);
  if (!currentJob.sensorArmed) {
    if (!detected) {
      currentJob.sensorArmed = true;
    }
    return;
  }

  if (!detected) return;

  ++currentJob.dispensedCount;
  currentJob.sensorArmed = false;
  Serial.print(F("EVENT motor="));
  Serial.print(currentJob.motor);
  Serial.print(F(" dispensed="));
  Serial.println(currentJob.dispensedCount);

  if (currentJob.dispensedCount >= currentJob.requestedCount) {
    finishJob();
  }
}

void stopAll() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    stopMotor(i);
  }
  currentJob = {};
  Serial.println(F("OK STOP"));
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  if (command == F("PING")) {
    Serial.println(F("PONG"));
    return;
  }
  if (command == F("STATUS")) {
    printStatus();
    return;
  }
  if (command == F("STOP")) {
    stopAll();
    return;
  }
  if (command == F("HELP")) {
    printHelp();
    return;
  }

  if (command.startsWith(F("DISPENSE "))) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }

    const uint8_t motorNumber = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace).toInt());
    const uint8_t count = static_cast<uint8_t>(command.substring(secondSpace + 1).toInt());
    startDispense(motorNumber, count);
    return;
  }

  Serial.println(F("ERR unknown"));
}

void readCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      handleCommand(commandBuffer);
      commandBuffer = "";
      continue;
    }
    if (commandBuffer.length() < 96) {
      commandBuffer += incoming;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (MOTOR_CFG[i].irPin != INVALID_PIN) {
      pinMode(MOTOR_CFG[i].irPin, INPUT_PULLUP);
    }
  }

  Serial.println(F("ESP32 COIN SERVO CONTROLLER READY"));
  printHelp();
}

void loop() {
  readCommands();
  serviceDispense();
}
