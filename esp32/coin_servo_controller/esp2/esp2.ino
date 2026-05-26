#include <Arduino.h>
#include <WiFi.h>

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr long ESP1_UART_BAUD = 115200;
constexpr int ESP1_UART_RX_PIN = 32;
constexpr int ESP1_UART_TX_PIN = 4;
constexpr const char* WIFI_SSID = "CASHWIFI";
constexpr const char* WIFI_PASSWORD = "CASH12345!";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint8_t MOTOR_COUNT = 3;  // ESP2 controls motors 1..3 only
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

// Pin map for ESP32 + 3x ULN2003 stepper driver channels + 3x IR sensors.
// Motor index meaning:
// 1 = 1 peso, 2 = 5 peso, 3 = 10 peso.
// Keep original coil mapping for easier wiring.
// IR3 is moved off GPIO12 to avoid boot-strap issues.
// IR sensors detect coin falls:
// Motor 1: IR pin 34, Motor 2: IR pin 35, Motor 3: IR pin 33
MotorConfig MOTOR_CFG[MOTOR_COUNT] = {
  {13, 14, 16, 17, 34, true, true, 1400, 200, 0, 1},   // 1 peso: coils 13/14/16/17, IR 34 (0 = no timeout)
  {18, 19, 21, 22, 35, true, true, 1400, 200, 0, 5},   // 5 peso: coils 18/19/21/22, IR 35 (0 = no timeout)
  {23, 25, 26, 27, 33, true, true, 1400, 200, 0, 10},  // 10 peso: coils 23/25/26/27, IR 33 (0 = no timeout)
};

struct DispenseJob {
  bool active;
  uint8_t motor;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  bool goingForward;
  unsigned long startedMs;
  unsigned long settleUntilMs;
  unsigned long phaseStartedMs;
  unsigned long lastStepUs;
};

enum QaTestPhase {
  QA_IDLE,
  QA_FORWARD,
  QA_BACKWARD,
  QA_WAIT_COIN,
  QA_COMPLETE
};

struct QaTest {
  bool active;
  uint8_t motor;
  QaTestPhase phase;
  uint8_t cycleCount;
  uint8_t maxCycles;
  bool stopOnCoin;
  bool retractAfterDetect;
  unsigned long phaseStartedMs;
  unsigned long lastIrReportMs;
  unsigned long lastStepUs;
};

constexpr unsigned long QA_FORWARD_MS = 10000;
constexpr unsigned long QA_BACKWARD_MS = 10000;
constexpr unsigned long QA_MOTOR3_BACKWARD_EXTRA_MS = 3000;
constexpr unsigned long QA_IR_REPORT_INTERVAL_MS = 500;
constexpr unsigned long QA_PHASE_TIMEOUT_MS = 60000;
constexpr unsigned long MOTION_DELAY_MS = 500; // delay between direction changes
constexpr unsigned long DISPENSE_SWEEP_FORWARD_MS = 13000;
constexpr unsigned long DISPENSE_SWEEP_BACKWARD_MS = 10000;

void serviceStepper(uint8_t motorIndex);

uint8_t stepIndex[MOTOR_COUNT] = {0, 0, 0};
DispenseJob currentJob = {};
QaTest qaTest = {};
String commandBuffer;
String esp1CommandBuffer;
HardwareSerial Esp1Serial(2);

void sendEsp1Line(const String& line) {
  Esp1Serial.println(line);
}

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

void stepMotorOnceDirection(uint8_t motorIndex, bool forward) {
  if (motorIndex >= MOTOR_COUNT) return;
  const uint8_t sequenceIndex = stepIndex[motorIndex];

  writeCoils(
    motorIndex,
    HALFSTEP_SEQ[sequenceIndex][0],
    HALFSTEP_SEQ[sequenceIndex][1],
    HALFSTEP_SEQ[sequenceIndex][2],
    HALFSTEP_SEQ[sequenceIndex][3]
  );

  if (forward) {
    stepIndex[motorIndex] = (sequenceIndex + 1) & 0x07;
  } else {
    stepIndex[motorIndex] = (sequenceIndex + 7) & 0x07;
  }
}

void serviceQaStepper(uint8_t motorIndex, bool forward) {
  if (motorIndex >= MOTOR_COUNT) return;
  const unsigned long nowUs = micros();
  if (nowUs - qaTest.lastStepUs < MOTOR_CFG[motorIndex].stepIntervalUs) return;
  qaTest.lastStepUs = nowUs;
  stepMotorOnceDirection(motorIndex, forward);
}

bool isIrDetected(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT || !isMotorConfigured(motorIndex)) return false;
  if (MOTOR_CFG[motorIndex].irPin == INVALID_PIN) return false;
  const int raw = digitalRead(MOTOR_CFG[motorIndex].irPin);
  return MOTOR_CFG[motorIndex].irActiveLow ? (raw == LOW) : (raw == HIGH);
}

void startQaTest(uint8_t motorNumber, uint8_t maxCycles) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT || maxCycles == 0) {
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
  qaTest.active = true;
  qaTest.motor = motorNumber;
  qaTest.phase = QA_FORWARD;
  qaTest.cycleCount = 0;
  qaTest.maxCycles = maxCycles;
  qaTest.stopOnCoin = false;
  qaTest.retractAfterDetect = false;
  qaTest.phaseStartedMs = millis();
  qaTest.lastIrReportMs = 0;
  qaTest.lastStepUs = micros();
  releaseCoils(motorIndex);
  Serial.print(F("OK QA_TEST motor="));
  Serial.print(motorNumber);
  Serial.print(F(" cycles="));
  Serial.println(maxCycles);
}

void startCoinDropTest(uint8_t motorNumber) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT) {
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

  qaTest.active = true;
  qaTest.motor = motorNumber;
  qaTest.phase = QA_FORWARD;
  qaTest.cycleCount = 0;
  qaTest.maxCycles = 255;
  qaTest.stopOnCoin = true;
  qaTest.retractAfterDetect = false;
  qaTest.phaseStartedMs = millis();
  qaTest.lastIrReportMs = 0;
  qaTest.lastStepUs = micros();
  releaseCoils(motorIndex);

  Serial.print(F("OK COIN_TEST motor="));
  Serial.println(motorNumber);
  Serial.println(F("COIN_TEST mode=run-until-detected"));
}

void serviceQaTest() {
  if (!qaTest.active) return;
  
  const uint8_t motorIndex = qaTest.motor - 1;
  const unsigned long nowMs = millis();
  const unsigned long phaseElapsedMs = nowMs - qaTest.phaseStartedMs;
  const unsigned long backwardTargetMs = QA_BACKWARD_MS + ((motorIndex == 2) ? QA_MOTOR3_BACKWARD_EXTRA_MS : 0);
  const bool coinDetected = isIrDetected(motorIndex);
  
  // Report IR sensor status periodically
  if (nowMs - qaTest.lastIrReportMs >= QA_IR_REPORT_INTERVAL_MS) {
    qaTest.lastIrReportMs = nowMs;
    Serial.print(F("QA motor="));
    Serial.print(qaTest.motor);
    Serial.print(F(" phase="));
    Serial.print((int)qaTest.phase);
    Serial.print(F(" ir="));
    Serial.print(coinDetected ? F("COIN_DETECTED") : F("no_coin"));
    Serial.print(F(" cycle="));
    Serial.print(qaTest.cycleCount);
    Serial.print(F("/"));
    Serial.println(qaTest.maxCycles);
  }

  if (qaTest.stopOnCoin && !qaTest.retractAfterDetect && coinDetected) {
    // Coin detected: force a full backward retract so the pusher resets
    // to a ready position before stopping.
    qaTest.retractAfterDetect = true;
    qaTest.phase = QA_BACKWARD;
    qaTest.phaseStartedMs = nowMs;
    qaTest.lastStepUs = micros();
    Serial.print(F("COIN_TEST detected motor="));
    Serial.println(qaTest.motor);
    Serial.print(F("COIN_TEST retract motor="));
    Serial.println(qaTest.motor);
  }
  
  // Phase timeout safety
  if (phaseElapsedMs > QA_PHASE_TIMEOUT_MS) {
    Serial.print(F("ERR QA timeout motor="));
    Serial.print(qaTest.motor);
    Serial.print(F(" phase="));
    Serial.println((int)qaTest.phase);
    qaTest.active = false;
    releaseCoils(motorIndex);
    return;
  }
  
  // State machine for forward -> backward -> repeat
  switch (qaTest.phase) {
    case QA_FORWARD:
      serviceQaStepper(motorIndex, true);
      if (phaseElapsedMs >= QA_FORWARD_MS) {
        qaTest.phase = QA_BACKWARD;
        qaTest.phaseStartedMs = nowMs;
        qaTest.lastStepUs = micros();
        Serial.print(F("QA phase_change motor="));
        Serial.print(qaTest.motor);
        Serial.println(F(" FORWARD -> BACKWARD"));
      }
      break;
      
    case QA_BACKWARD:
      serviceQaStepper(motorIndex, false);
      if (phaseElapsedMs >= backwardTargetMs) {
        if (qaTest.stopOnCoin && qaTest.retractAfterDetect) {
          stepIndex[motorIndex] = 0;
          releaseCoils(motorIndex);
          qaTest.active = false;
          Serial.print(F("COIN_TEST ready motor="));
          Serial.println(qaTest.motor);
          return;
        }
        delay(MOTION_DELAY_MS);  // Pause before next cycle
        ++qaTest.cycleCount;
        if (qaTest.cycleCount >= qaTest.maxCycles) {
          qaTest.phase = QA_COMPLETE;
          Serial.print(F("QA complete motor="));
          Serial.print(qaTest.motor);
          Serial.print(F(" cycles="));
          Serial.println(qaTest.cycleCount);
        } else {
          qaTest.phase = QA_FORWARD;
          qaTest.phaseStartedMs = nowMs;
          qaTest.lastStepUs = micros();
          Serial.print(F("QA phase_change motor="));
          Serial.print(qaTest.motor);
          Serial.println(F(" BACKWARD -> FORWARD"));
        }
      }
      break;
      
    case QA_COMPLETE:
      stepIndex[motorIndex] = 0;  // Reset step position
      releaseCoils(motorIndex);    // Explicitly de-energize coils
      qaTest.active = false;
      Serial.print(F("QA_TEST finished motor="));
      Serial.print(qaTest.motor);
      Serial.print(F(" totalCycles="));
      Serial.println(qaTest.cycleCount);
      break;
      
    default:
      qaTest.active = false;
      break;
  }
}

void stopQaTest() {
  if (qaTest.active) {
    const uint8_t motorIndex = qaTest.motor - 1;
    stepIndex[motorIndex] = 0;  // Reset step position
    releaseCoils(motorIndex);    // Explicitly de-energize coils
    qaTest.active = false;
    Serial.println(F("OK QA_TEST stopped"));
  }
}

void startSensorDebug(uint8_t motorNumber, uint8_t durationSeconds) {
  if (motorNumber < 1 || motorNumber > MOTOR_COUNT) {
    Serial.println(F("ERR invalid motor"));
    return;
  }
  const uint8_t motorIndex = motorNumber - 1;
  if (MOTOR_CFG[motorIndex].irPin == INVALID_PIN) {
    Serial.print(F("ERR motor="));
    Serial.print(motorNumber);
    Serial.println(F(" reason=no-ir-sensor"));
    return;
  }
  Serial.print(F("SENSOR_DEBUG motor="));
  Serial.print(motorNumber);
  Serial.print(F(" duration="));
  Serial.print(durationSeconds);
  Serial.println(F(" seconds"));
  
  const unsigned long endMs = millis() + (durationSeconds * 1000UL);
  while (millis() < endMs) {
    const bool detected = isIrDetected(motorIndex);
    const int raw = digitalRead(MOTOR_CFG[motorIndex].irPin);
    Serial.print(F("IR motor="));
    Serial.print(motorNumber);
    Serial.print(F(" raw="));
    Serial.print(raw);
    Serial.print(F(" detected="));
    Serial.println(detected ? F("YES") : F("NO"));
    delay(200);
  }
  Serial.println(F("SENSOR_DEBUG complete"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  PING"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  DISPENSE <motor 1..3> <count>"));
  Serial.println(F("  DISPENSE_DENOM <1|5|10> <count>"));
  Serial.println(F("  TEST <motor 1..3> [cycles=5]"));
  Serial.println(F("  COIN_TEST <motor 1..3>"));
  Serial.println(F("  SENSOR_DEBUG <motor 1..3> <duration_sec>"));
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

  // After successful dispense, force a full backward retract for 10s.
  const unsigned long retractStartedMs = millis();
  unsigned long retractLastStepUs = micros();
  while (millis() - retractStartedMs < DISPENSE_SWEEP_BACKWARD_MS) {
    const unsigned long nowUs = micros();
    if (nowUs - retractLastStepUs >= MOTOR_CFG[motorIndex].stepIntervalUs) {
      retractLastStepUs = nowUs;
      stepMotorOnceDirection(motorIndex, false);
    }
  }

  // Reset step position and release coils
  stepIndex[motorIndex] = 0;
  releaseCoils(motorIndex);
  // Clear job state now that dispense is complete to avoid timeout/ERR afterward.
  currentJob = {};

  // Log the completion
  Serial.print(F("DONE motor="));
  Serial.print(motorIndex + 1);
  Serial.print(F(" count="));
  Serial.print(dispensed);
  Serial.print(F(" amount="));
  Serial.println(dispensed * MOTOR_CFG[motorIndex].denomination);

  // Send completion message to ESP1
  String line = F("DONE motor=");
  line += String(motorIndex + 1);
  line += F(" count=");
  line += String(dispensed);
  line += F(" amount=");
  line += String(dispensed * MOTOR_CFG[motorIndex].denomination);
  sendEsp1Line(line);
  Serial.println(F("Sending DONE message to ESP1"));
}

void failJob(const char* reason) {
  const uint8_t motor = currentJob.motor;
  bool irDetected = false;
  int irRaw = -1;
  bool sensorArmed = currentJob.sensorArmed;
  uint8_t dispensed = currentJob.dispensedCount;

  if (motor >= 1 && motor <= MOTOR_COUNT) {
    const uint8_t motorIndex = motor - 1;
    if (isMotorConfigured(motorIndex) && MOTOR_CFG[motorIndex].irPin != INVALID_PIN) {
      irDetected = isIrDetected(motorIndex);
      irRaw = digitalRead(MOTOR_CFG[motorIndex].irPin);
    }
  }

  if (motor >= 1 && motor <= MOTOR_COUNT) {
    const uint8_t motorIndex = motor - 1;
    stepIndex[motorIndex] = 0;  // Reset step position
    releaseCoils(motorIndex);     // Explicitly de-energize coils
  }
  currentJob = {};
  Serial.print(F("ERR motor="));
  Serial.print(motor);
  Serial.print(F(" reason="));
  Serial.print(reason);
  Serial.print(F(" dispensed="));
  Serial.print(dispensed);
  Serial.print(F(" armed="));
  Serial.print(sensorArmed ? F("1") : F("0"));
  Serial.print(F(" irRaw="));
  Serial.print(irRaw);
  Serial.print(F(" irDetected="));
  Serial.println(irDetected ? F("1") : F("0"));

  String line = F("ERR motor=");
  line += String(motor);
  line += F(" reason=");
  line += reason;
  line += F(" dispensed=");
  line += String(dispensed);
  line += F(" armed=");
  line += (sensorArmed ? F("1") : F("0"));
  line += F(" irRaw=");
  line += String(irRaw);
  line += F(" irDetected=");
  line += (irDetected ? F("1") : F("0"));
  sendEsp1Line(line);
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
  currentJob.goingForward = true;
  currentJob.startedMs = millis();
  currentJob.settleUntilMs = currentJob.startedMs + MOTOR_CFG[motorIndex].startupSettleMs;
  currentJob.phaseStartedMs = currentJob.startedMs;
  currentJob.lastStepUs = micros();

  Serial.print(F("OK DISPENSE motor="));
  Serial.print(motorNumber);
  Serial.print(F(" count="));
  Serial.println(count);

  String line = F("OK DISPENSE motor=");
  line += String(motorNumber);
  line += F(" count=");
  line += String(count);
  sendEsp1Line(line);
}

void serviceStepper(uint8_t motorIndex) {
  if (motorIndex >= MOTOR_COUNT) return;
  const unsigned long nowUs = micros();
  if (nowUs - currentJob.lastStepUs < MOTOR_CFG[motorIndex].stepIntervalUs) return;
  currentJob.lastStepUs = nowUs;
  stepMotorOnce(motorIndex);
}

void serviceStepperDirection(uint8_t motorIndex, bool forward) {
  if (motorIndex >= MOTOR_COUNT) return;
  const unsigned long nowUs = micros();
  if (nowUs - currentJob.lastStepUs < MOTOR_CFG[motorIndex].stepIntervalUs) return;
  currentJob.lastStepUs = nowUs;
  stepMotorOnceDirection(motorIndex, forward);
}

unsigned long forwardSweepMsForMotor(uint8_t motorIndex) {
  (void)motorIndex;
  return DISPENSE_SWEEP_FORWARD_MS;
}

unsigned long backwardSweepMsForMotor(uint8_t motorIndex) {
  (void)motorIndex;
  return DISPENSE_SWEEP_BACKWARD_MS;
}

void serviceDispense() {
  if (!currentJob.active) return;

  const uint8_t motorIndex = currentJob.motor - 1;
  const unsigned long nowMs = millis();

  // Check for timeout only when timeout is configured (>0).
  if (MOTOR_CFG[motorIndex].timeoutMs > 0 &&
      nowMs - currentJob.startedMs >= MOTOR_CFG[motorIndex].timeoutMs) {
    failJob("timeout");
    return;
  }

  // Sweep forward/backward until IR confirms coin drop.
  const unsigned long phaseElapsedMs = nowMs - currentJob.phaseStartedMs;
  if (currentJob.goingForward) {
    if (phaseElapsedMs >= forwardSweepMsForMotor(motorIndex)) {
      currentJob.goingForward = false;
      currentJob.phaseStartedMs = nowMs;
      Serial.print(F("SWEEP motor="));
      Serial.print(currentJob.motor);
      Serial.println(F(" dir=BACKWARD"));
    }
  } else {
    if (phaseElapsedMs >= backwardSweepMsForMotor(motorIndex)) {
      currentJob.goingForward = true;
      currentJob.phaseStartedMs = nowMs;
      Serial.print(F("SWEEP motor="));
      Serial.print(currentJob.motor);
      Serial.println(F(" dir=FORWARD"));
    }
  }

  serviceStepperDirection(motorIndex, currentJob.goingForward);

  // Wait for the motor to settle after startup
  if (nowMs < currentJob.settleUntilMs) return;

  // Check IR sensor state
  const bool detected = isIrDetected(motorIndex);
  if (!currentJob.sensorArmed) {
    if (!detected) {
      currentJob.sensorArmed = true;
    }
    return;
  }

  if (!detected) return;

  // Increment dispensed count and reset sensor state
  ++currentJob.dispensedCount;
  currentJob.sensorArmed = false;
  Serial.print(F("EVENT motor="));
  Serial.print(currentJob.motor);
  Serial.print(F(" dispensed="));
  Serial.println(currentJob.dispensedCount);

    delay(MOTION_DELAY_MS);  // Pause before next cycle

  // After each detected coin, retract before next push cycle.
  currentJob.goingForward = false;
  currentJob.phaseStartedMs = nowMs;

  // Finish the job if the requested count is reached
  if (currentJob.dispensedCount >= currentJob.requestedCount) {
    finishJob();
  }
}

void stopAll() {
  stopQaTest();
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    stepIndex[i] = 0;
    releaseCoils(i);
  }
  currentJob = {};
  Serial.println(F("OK STOP"));
  sendEsp1Line(F("OK STOP"));
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  if (command == F("PING")) {
    Serial.println(F("PONG"));
    sendEsp1Line(F("PONG"));
    return;
  }
  if (command == F("STATUS")) {
    printStatus();
    String line = F("STATUS active=");
    line += (currentJob.active ? F("1") : F("0"));
    if (currentJob.active) {
      line += F(" motor=");
      line += String(currentJob.motor);
      line += F(" done=");
      line += String(currentJob.dispensedCount);
      line += '/';
      line += String(currentJob.requestedCount);
    }
    sendEsp1Line(line);
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

  if (command.startsWith(F("TEST "))) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    const uint8_t motorNumber = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace >= 0 ? secondSpace : command.length()).toInt());
    const uint8_t cycles = (secondSpace >= 0) ? static_cast<uint8_t>(command.substring(secondSpace + 1).toInt()) : 5;
    startQaTest(motorNumber, cycles > 0 ? cycles : 5);
    return;
  }

  if (command.startsWith(F("COIN_TEST "))) {
    int firstSpace = command.indexOf(' ');
    const uint8_t motorNumber = static_cast<uint8_t>(command.substring(firstSpace + 1).toInt());
    startCoinDropTest(motorNumber);
    return;
  }

  if (command.startsWith(F("SENSOR_DEBUG "))) {
    int firstSpace = command.indexOf(' ');
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }
    const uint8_t motorNumber = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace).toInt());
    const uint8_t durationSeconds = static_cast<uint8_t>(command.substring(secondSpace + 1).toInt());
    startSensorDebug(motorNumber, durationSeconds > 0 ? durationSeconds : 10);
    return;
  }

  Serial.println(F("ERR unknown"));
}

void readCommandsFromPort(Stream& port, String& buffer) {
  while (port.available() > 0) {
    const char incoming = static_cast<char>(port.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      handleCommand(buffer);
      buffer = "";
      continue;
    }
    if (buffer.length() < 96) {
      buffer += incoming;
    }
  }
}

void readCommands() {
  readCommandsFromPort(Serial, commandBuffer);
  readCommandsFromPort(Esp1Serial, esp1CommandBuffer);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  Serial.print(F("WIFI connecting ssid="));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startedMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startedMs) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WIFI connected ip="));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WIFI not connected (continuing offline mode)"));
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  Esp1Serial.begin(ESP1_UART_BAUD, SERIAL_8N1, ESP1_UART_RX_PIN, ESP1_UART_TX_PIN);
  connectWifi();

  // CRITICAL: Set all stepper pins to LOW immediately to prevent ULN drivers from overheating at boot
  // ESP32 GPIO defaults to HIGH on boot, which would energize all coils
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (MOTOR_CFG[i].in1 != INVALID_PIN) {
      pinMode(MOTOR_CFG[i].in1, OUTPUT);
      digitalWrite(MOTOR_CFG[i].in1, LOW);
    }
    if (MOTOR_CFG[i].in2 != INVALID_PIN) {
      pinMode(MOTOR_CFG[i].in2, OUTPUT);
      digitalWrite(MOTOR_CFG[i].in2, LOW);
    }
    if (MOTOR_CFG[i].in3 != INVALID_PIN) {
      pinMode(MOTOR_CFG[i].in3, OUTPUT);
      digitalWrite(MOTOR_CFG[i].in3, LOW);
    }
    if (MOTOR_CFG[i].in4 != INVALID_PIN) {
      pinMode(MOTOR_CFG[i].in4, OUTPUT);
      digitalWrite(MOTOR_CFG[i].in4, LOW);
    }
    if (MOTOR_CFG[i].irPin != INVALID_PIN) {
      if (MOTOR_CFG[i].irPin >= 34) {
        pinMode(MOTOR_CFG[i].irPin, INPUT);
      } else {
        pinMode(MOTOR_CFG[i].irPin, INPUT_PULLUP);
      }
    }
    releaseCoils(i);
  }

  Serial.println(F("ESP2 - COIN PUSHER STEPPER CONTROLLER READY"));
  Serial.print(F("ESP2 UART link RX="));
  Serial.print(ESP1_UART_RX_PIN);
  Serial.print(F(" TX="));
  Serial.println(ESP1_UART_TX_PIN);
  printHelp();
}

void loop() {
  readCommands();
  serviceDispense();
  serviceQaTest();
  
  // Safety failsafe: if no job is active, ensure all coils are released
  if (!currentJob.active && !qaTest.active) {
    static unsigned long lastReleaseMs = 0;
    const unsigned long nowMs = millis();
    if (nowMs - lastReleaseMs >= 100) {  // Check every 100ms
      lastReleaseMs = nowMs;
      for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
        stepIndex[i] = 0;
        releaseCoils(i);
      }
    }
  }
}
