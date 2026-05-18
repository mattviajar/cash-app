#include <Arduino.h>

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t MOTOR_COUNT = 5;
constexpr uint8_t INVALID_PIN = 255;
constexpr uint8_t HALFSTEP_COUNT = 8;

const uint8_t HALFSTEP_SEQ[HALFSTEP_COUNT][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1},
};

struct MotorConfig {
  uint8_t in1;
  uint8_t in2;
  uint8_t in3;
  uint8_t in4;
  uint8_t irPin;
  bool irActiveLow;
  bool forward;
  unsigned long stepIntervalUs;
  unsigned long startupSettleMs;
  unsigned long timeoutMs;
  uint8_t denomination;
};

// Default pin map for ESP32-WROOM-32 + 5x ULN2003 channels.
// Motor index meaning:
// 1 = 1 peso, 2 = 5 peso, 3 = 10 peso, 4 = 20 peso, 5 = extra channel.
// Note: On classic ESP32, only four input-only GPIOs are normally available
// for dedicated IR lines (34, 35, 36, 39) while keeping USB Serial on 1/3.
// Channel 5 is wired for stepper control, but IR5 is left INVALID by default.
MotorConfig MOTOR_CFG[MOTOR_COUNT] = {
  {13, 14, 16, 17, 34, true, true, 1400, 200, 18000, 1},
  {18, 19, 21, 22, 35, true, true, 1400, 200, 18000, 5},
  {23, 25, 26, 27, 36, true, true, 1400, 200, 18000, 10},
  {32, 33, 4, 5, 39, true, true, 1400, 200, 18000, 20},
  {12, 15, 2, 0, INVALID_PIN, true, true, 1400, 200, 18000, 0},
};

struct DispenseJob {
  bool active;
  uint8_t motor;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  unsigned long startedMs;
  unsigned long settleUntilMs;
  unsigned long lastStepUs;
};

uint8_t stepIndex[MOTOR_COUNT] = {0, 0, 0, 0, 0};
DispenseJob currentJob = {};
String commandBuffer;

bool isMotorConfigured(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return false;
  const MotorConfig& cfg = MOTOR_CFG[motorIndex];
  return cfg.in1 != INVALID_PIN && cfg.in2 != INVALID_PIN && cfg.in3 != INVALID_PIN &&
         cfg.in4 != INVALID_PIN;
}

void writeCoils(uint8_t motorIndex, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  if (motorIndex >= MOTOR_COUNT) return;
  const MotorConfig& cfg = MOTOR_CFG[motorIndex];
  digitalWrite(cfg.in1, a);
  digitalWrite(cfg.in2, b);
  digitalWrite(cfg.in3, c);
  digitalWrite(cfg.in4, d);
}

void releaseCoils(uint8_t motorIndex) {
  writeCoils(motorIndex, 0, 0, 0, 0);
}

void stepMotorOnce(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return;
  const MotorConfig& cfg = MOTOR_CFG[motorIndex];
  const uint8_t sequenceIndex = stepIndex[motorIndex];

  writeCoils(
    motorIndex,
    HALFSTEP_SEQ[sequenceIndex][0],
    HALFSTEP_SEQ[sequenceIndex][1],
    HALFSTEP_SEQ[sequenceIndex][2],
    HALFSTEP_SEQ[sequenceIndex][3]
  );

  if (cfg.forward) {
    stepIndex[motorIndex] = (sequenceIndex + 1) & 0x07;
  } else {
    stepIndex[motorIndex] = (sequenceIndex + 7) & 0x07;
  }
}

bool isIrDetected(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT || !isMotorConfigured(motorIndex)) return false;
  if (MOTOR_CFG[motorIndex].irPin == INVALID_PIN) return false;
  const int raw = digitalRead(MOTOR_CFG[motorIndex].irPin);
  return MOTOR_CFG[motorIndex].irActiveLow ? (raw == LOW) : (raw == HIGH);
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  PING"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  DISPENSE <motor 1..5> <count>"));
  Serial.println(F("  DISPENSE_DENOM <1|5|10|20> <count>"));
  Serial.println(F("  STOP"));
  Serial.println(F("  HELP"));
}

int8_t motorIndexByDenomination(uint8_t denomination) {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (MOTOR_CFG[i].denomination == denomination) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
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
    if (isMotorConfigured(i) && MOTOR_CFG[i].irPin != INVALID_PIN) {
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
  releaseCoils(motorIndex);
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
    releaseCoils(motor - 1);
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

  currentJob.active = true;
  currentJob.motor = motorNumber;
  currentJob.requestedCount = count;
  currentJob.dispensedCount = 0;
  if (MOTOR_CFG[motorIndex].irPin == INVALID_PIN) {
    Serial.print(F("ERR motor="));
    Serial.print(motorNumber);
    Serial.println(F(" reason=ir-not-configured"));
    currentJob = {};
    return;
  }
  currentJob.sensorArmed = !isIrDetected(motorIndex);
  currentJob.startedMs = millis();
  currentJob.settleUntilMs = currentJob.startedMs + MOTOR_CFG[motorIndex].startupSettleMs;
  currentJob.lastStepUs = micros();

  Serial.print(F("OK DISPENSE motor="));
  Serial.print(motorNumber);
  Serial.print(F(" count="));
  Serial.println(count);
}

void serviceStepper(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return;
  const unsigned long nowUs = micros();
  if (nowUs - currentJob.lastStepUs < MOTOR_CFG[motorIndex].stepIntervalUs) return;
  currentJob.lastStepUs = nowUs;
  stepMotorOnce(motorIndex);
}

void serviceDispense() {
  if (!currentJob.active) return;

  const uint8_t motorIndex = currentJob.motor - 1;
  const unsigned long nowMs = millis();

  if (nowMs - currentJob.startedMs >= MOTOR_CFG[motorIndex].timeoutMs) {
    failJob(F("timeout"));
    return;
  }

  serviceStepper(motorIndex);

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
    releaseCoils(i);
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

  if (command.startsWith(F("DISPENSE_DENOM "))) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }

    const uint8_t denomination = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace).toInt());
    const uint8_t count = static_cast<uint8_t>(command.substring(secondSpace + 1).toInt());
    const int8_t motorIndex = motorIndexByDenomination(denomination);
    if (motorIndex < 0) {
      Serial.print(F("ERR denom="));
      Serial.print(denomination);
      Serial.println(F(" reason=not-mapped"));
      return;
    }
    startDispense(static_cast<uint8_t>(motorIndex + 1), count);
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
    if (MOTOR_CFG[i].in1 != INVALID_PIN) pinMode(MOTOR_CFG[i].in1, OUTPUT);
    if (MOTOR_CFG[i].in2 != INVALID_PIN) pinMode(MOTOR_CFG[i].in2, OUTPUT);
    if (MOTOR_CFG[i].in3 != INVALID_PIN) pinMode(MOTOR_CFG[i].in3, OUTPUT);
    if (MOTOR_CFG[i].in4 != INVALID_PIN) pinMode(MOTOR_CFG[i].in4, OUTPUT);
    if (MOTOR_CFG[i].irPin != INVALID_PIN) {
      if (MOTOR_CFG[i].irPin >= 34) {
        pinMode(MOTOR_CFG[i].irPin, INPUT);
      } else {
        pinMode(MOTOR_CFG[i].irPin, INPUT_PULLUP);
      }
    }
    releaseCoils(i);
  }

  Serial.println(F("ESP32 COIN STEPPER CONTROLLER READY"));
  printHelp();
}

void loop() {
  readCommands();
  serviceDispense();
}
