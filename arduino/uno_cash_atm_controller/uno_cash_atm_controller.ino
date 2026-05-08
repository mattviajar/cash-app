// Arduino Uno 1 final dispenser controller
// Controls Motors 1-3 and reports completion back to the ESP32 master.

namespace {

constexpr long SERIAL_BAUD = 9600;
constexpr bool ACTIVE_LOW = true;
constexpr bool MOTORS_ENABLED = true;
constexpr unsigned long DETECT_HOLD_MS = 120;
constexpr unsigned long STARTUP_GRACE_MS = 1500;
constexpr unsigned long POST_DETECT_SETTLE_MS = 180;
constexpr unsigned long COIN_TIMEOUT_MS = 12000;
constexpr unsigned long STATUS_PRINT_MS = 500;
constexpr uint8_t IR4_PIN = 17;  // A3 — coin motor IR sensor

constexpr uint8_t HALF_STEP[8][4] = {
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
  unsigned long stepIntervalUs;
  uint8_t denomination;
};

struct MotorRuntime {
  int sequenceIndex;
};

struct DispenseJob {
  bool active;
  uint8_t motorIndex;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  unsigned long startedMs;
  unsigned long coinDeadlineMs;
  unsigned long settleUntilMs;
  unsigned long detectStartedMs;
  unsigned long lastStepUs;
};

constexpr MotorConfig MOTORS[] = {
  {2, 3, 4, 5, A0, 2400, 1},
  {6, 7, 8, 9, 15, 2600, 5},
  {10, 11, 12, 13, 16, 2400, 10},
};

MotorRuntime motorState[3] = {};
DispenseJob currentJob = {};
String commandBuffer;
unsigned long startupMs = 0;
unsigned long lastStatusPrintMs = 0;
bool ir4LastLevel = HIGH;

bool isDetected(int rawValue) {
  return ACTIVE_LOW ? (rawValue == LOW) : (rawValue == HIGH);
}

void writeCoils(uint8_t motorIndex, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  const MotorConfig& motor = MOTORS[motorIndex];
  digitalWrite(motor.in1, a);
  digitalWrite(motor.in2, b);
  digitalWrite(motor.in3, c);
  digitalWrite(motor.in4, d);
}

void releaseMotor(uint8_t motorIndex) {
  writeCoils(motorIndex, 0, 0, 0, 0);
}

void releaseAllMotors() {
  for (uint8_t motorIndex = 0; motorIndex < 3; ++motorIndex) {
    releaseMotor(motorIndex);
  }
}

void stepForward(uint8_t motorIndex) {
  MotorRuntime& runtime = motorState[motorIndex];
  writeCoils(
    motorIndex,
    HALF_STEP[runtime.sequenceIndex][0],
    HALF_STEP[runtime.sequenceIndex][1],
    HALF_STEP[runtime.sequenceIndex][2],
    HALF_STEP[runtime.sequenceIndex][3]
  );
  runtime.sequenceIndex = (runtime.sequenceIndex + 1) & 0x07;
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  PING"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  DISPENSE <motor 1..3> <count>"));
  Serial.println(F("  STOP"));
  Serial.println(F("  HELP"));
}

void printStatus() {
  Serial.print(F("STATUS active="));
  Serial.print(currentJob.active ? F("1") : F("0"));
  if (currentJob.active) {
    Serial.print(F(" motor="));
    Serial.print(currentJob.motorIndex + 1);
    Serial.print(F(" done="));
    Serial.print(currentJob.dispensedCount);
    Serial.print(F("/"));
    Serial.print(currentJob.requestedCount);
  }
  for (uint8_t motorIndex = 0; motorIndex < 3; ++motorIndex) {
    Serial.print(F(" ir"));
    Serial.print(motorIndex + 1);
    Serial.print('=');
    Serial.print(digitalRead(MOTORS[motorIndex].irPin));
  }
  Serial.print(F(" ir4="));
  Serial.print(digitalRead(IR4_PIN));
  Serial.println();
}

void serviceIr4Edge() {
  const bool level = digitalRead(IR4_PIN);
  if (level != ir4LastLevel) {
    ir4LastLevel = level;
    Serial.print(F("IR4_EDGE level="));
    Serial.println(level == LOW ? F("LOW") : F("HIGH"));
  }
}

void finishJob() {
  const uint8_t motorIndex = currentJob.motorIndex;
  const uint8_t dispensed = currentJob.dispensedCount;
  releaseMotor(motorIndex);
  currentJob = {};
  Serial.print(F("DONE motor="));
  Serial.print(motorIndex + 1);
  Serial.print(F(" count="));
  Serial.print(dispensed);
  Serial.print(F(" amount="));
  Serial.println(dispensed * MOTORS[motorIndex].denomination);
}

void failJob(const __FlashStringHelper* reason) {
  const uint8_t motorIndex = currentJob.motorIndex;
  releaseMotor(motorIndex);
  currentJob = {};
  Serial.print(F("ERR motor="));
  Serial.print(motorIndex + 1);
  Serial.print(F(" reason="));
  Serial.println(reason);
}

void startDispense(uint8_t motorNumber, uint8_t count) {
  if (!MOTORS_ENABLED) {
    releaseAllMotors();
    Serial.println(F("ERR motors-disabled"));
    return;
  }

  if (motorNumber < 1 || motorNumber > 3 || count == 0) {
    Serial.println(F("ERR invalid-args"));
    return;
  }
  if (currentJob.active) {
    Serial.println(F("ERR busy"));
    return;
  }

  const uint8_t motorIndex = motorNumber - 1;
  currentJob.active = true;
  currentJob.motorIndex = motorIndex;
  currentJob.requestedCount = count;
  currentJob.dispensedCount = 0;
  currentJob.sensorArmed = !isDetected(digitalRead(MOTORS[motorIndex].irPin));
  currentJob.startedMs = millis();
  currentJob.coinDeadlineMs = currentJob.startedMs + COIN_TIMEOUT_MS;
  currentJob.settleUntilMs = currentJob.startedMs + STARTUP_GRACE_MS;
  currentJob.detectStartedMs = 0;
  currentJob.lastStepUs = micros();

  Serial.print(F("OK DISPENSE motor="));
  Serial.print(motorNumber);
  Serial.print(F(" count="));
  Serial.println(count);
}

void stopAll() {
  releaseAllMotors();
  currentJob = {};
  Serial.println(F("OK STOP"));
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) {
    return;
  }

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

void readSerialCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      handleCommand(commandBuffer);
      commandBuffer = "";
      continue;
    }
    if (commandBuffer.length() < 64) {
      commandBuffer += incoming;
    }
  }
}

void serviceDispenseJob() {
  if (!MOTORS_ENABLED) {
    releaseAllMotors();
    if (currentJob.active) {
      failJob(F("motors-disabled"));
    }
    return;
  }

  if (!currentJob.active) {
    return;
  }

  const unsigned long nowMs = millis();
  const unsigned long nowUs = micros();
  const uint8_t motorIndex = currentJob.motorIndex;
  const MotorConfig& motor = MOTORS[motorIndex];

  if (nowMs >= currentJob.coinDeadlineMs) {
    failJob(F("timeout"));
    return;
  }

  if (nowMs < currentJob.settleUntilMs) {
    releaseMotor(motorIndex);
  } else if (nowUs - currentJob.lastStepUs >= motor.stepIntervalUs) {
    stepForward(motorIndex);
    currentJob.lastStepUs = nowUs;
  }

  const bool detected = isDetected(digitalRead(motor.irPin));
  if (!currentJob.sensorArmed) {
    if (!detected) {
      currentJob.sensorArmed = true;
      currentJob.detectStartedMs = 0;
    }
    return;
  }

  if (detected) {
    if (currentJob.detectStartedMs == 0) {
      currentJob.detectStartedMs = nowMs;
    } else if (nowMs - currentJob.detectStartedMs >= DETECT_HOLD_MS) {
      ++currentJob.dispensedCount;
      Serial.print(F("EVENT motor="));
      Serial.print(motorIndex + 1);
      Serial.print(F(" dispensed="));
      Serial.println(currentJob.dispensedCount);

      currentJob.sensorArmed = false;
      currentJob.detectStartedMs = 0;
      currentJob.coinDeadlineMs = nowMs + COIN_TIMEOUT_MS;
      currentJob.settleUntilMs = nowMs + POST_DETECT_SETTLE_MS;

      if (currentJob.dispensedCount >= currentJob.requestedCount) {
        finishJob();
      }
    }
  } else {
    currentJob.detectStartedMs = 0;
  }
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);

  for (uint8_t motorIndex = 0; motorIndex < 3; ++motorIndex) {
    pinMode(MOTORS[motorIndex].in1, OUTPUT);
    pinMode(MOTORS[motorIndex].in2, OUTPUT);
    pinMode(MOTORS[motorIndex].in3, OUTPUT);
    pinMode(MOTORS[motorIndex].in4, OUTPUT);
    pinMode(MOTORS[motorIndex].irPin, INPUT);
    releaseMotor(motorIndex);
  }
  pinMode(IR4_PIN, INPUT);
  ir4LastLevel = digitalRead(IR4_PIN);

  startupMs = millis();
  lastStatusPrintMs = startupMs;

  Serial.println(F("UNO CASH ATM CONTROLLER READY"));
  if (!MOTORS_ENABLED) {
    Serial.println(F("SAFE MODE: all Uno motors disabled"));
  }
  printHelp();
}

void loop() {
  readSerialCommands();
  serviceDispenseJob();
  serviceIr4Edge();

  const unsigned long nowMs = millis();
  if (nowMs - lastStatusPrintMs >= STATUS_PRINT_MS) {
    lastStatusPrintMs = nowMs;
    printStatus();
  }
}