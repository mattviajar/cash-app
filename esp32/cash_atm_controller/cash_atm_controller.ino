#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

namespace {

constexpr long USB_SERIAL_BAUD = 115200;
constexpr long UNO_SERIAL_BAUD = 9600;
constexpr uint8_t UNO_RX_PIN = 16;
constexpr uint8_t UNO_TX_PIN = 17;
constexpr bool STEPPERS_ENABLED = true;
constexpr bool SERVOS_ENABLED = true;
constexpr bool MOTION_ARMED_DEFAULT = true;
constexpr bool AUTO_LIFT_TO_IR5_1_ON_BOOT = true;
constexpr bool AUTO_LIFT_TO_BOTTOM_ON_BOOT = false;
constexpr bool AUTO_BILL_ROUTE_ENABLED = true;
constexpr bool ACCEPTORS_CONNECTED = true;
constexpr bool BILL_INPUT_ENABLED = true;
constexpr bool COIN_INPUT_ENABLED = true;
constexpr bool IR5_VERBOSE_LOG = false;

// IR sensors are wired active-low.
constexpr bool ACTIVE_LOW = true;
// Coin/bill acceptor outputs are typically open-collector and pull LOW on pulse.
constexpr bool PULSE_ACTIVE_LOW = true;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr unsigned long IR5_DETECT_HOLD_MS = 140;
constexpr unsigned long STARTUP_GRACE_MS = 1500;
constexpr unsigned long POST_DETECT_SETTLE_MS = 180;
constexpr unsigned long DISPENSE_TIMEOUT_MS = 12000;
constexpr unsigned long STATUS_PRINT_MS = 1000;
constexpr bool STATUS_VERBOSE_LOG = false;
constexpr unsigned long PULSE_MIN_EDGE_GAP_MS = 20;
constexpr unsigned long PULSE_MIN_WIDTH_MS = 15;
constexpr unsigned long PULSE_MAX_WIDTH_MS = 350;

constexpr uint8_t M4_IN1_PIN = 18;
constexpr uint8_t M4_IN2_PIN = 19;
constexpr uint8_t M4_IN3_PIN = 23;
constexpr uint8_t M4_IN4_PIN = 13;
constexpr uint8_t IR4_PIN = 27;

constexpr uint8_t BILL_PIN = 32;
constexpr uint8_t COIN_PIN = 14;
// IR5_1 moved to GPIO34 to avoid conflict with BILL_PIN on GPIO32.
constexpr uint8_t IR5_PINS[] = {34, 33, 25, 26, 4};

constexpr uint8_t SERVO_CHANNEL_COUNT = 7;
constexpr uint16_t SERVO_PWM_MIN = 110;
constexpr uint16_t SERVO_PWM_MAX = 510;
constexpr uint8_t PCA9685_ADDRESS = 0x40;
constexpr uint8_t PCA9685_MODE1 = 0x00;
constexpr uint8_t PCA9685_PRESCALE = 0xFE;
constexpr uint8_t PCA9685_LED0_ON_L = 0x06;

// PCA9685 channel assignment from your thesis hardware.
constexpr uint8_t SPINNER_CHANNEL_MIN = 0;   // CH0..CH4 storage spinner servos
constexpr uint8_t SPINNER_CHANNEL_MAX = 4;
constexpr uint8_t ELEVATOR_LIFT_CHANNEL = 5; // CH5 elevator lift servo
constexpr uint8_t ELEVATOR_OUT_CHANNEL = 6;  // CH6 elevator output spinner
constexpr bool LIFT_DIRECTION_INVERTED = true;

// 360 servo commands: 90=stop, >90 rotate one direction, <90 rotate opposite direction.
constexpr uint8_t SERVO_STOP_CMD = 90;
constexpr uint8_t SPINNER_RUN_CMD = 106;
// Reverse direction to eject bills from storage during withdrawal (max speed).
constexpr uint8_t SPINNER_WITHDRAW_CMD = 0;
constexpr uint8_t ELEVATOR_LIFT_UP_CMD = 122;
constexpr uint8_t ELEVATOR_LIFT_HOLD_CMD = 92;
constexpr uint8_t ELEVATOR_LIFT_CATCH_SLOW_UP_CMD = 110;
constexpr uint8_t ELEVATOR_LIFT_RECOVER_CMD = 114;
constexpr uint8_t ELEVATOR_LIFT_DOWN_CMD = 60;
constexpr uint8_t ELEVATOR_OUT_PUSH_CMD = 10;

constexpr unsigned long ELEVATOR_NUDGE_MS = 180;
constexpr unsigned long ELEVATOR_SETTLE_MS = 180;
constexpr unsigned long ELEVATOR_OUT_PULSE_MS = 5000;
constexpr unsigned long SPINNER_PULSE_MS = 300;
constexpr unsigned long ELEVATOR_RETURN_MS = 900;
constexpr unsigned long ELEVATOR_ROUTE_TIMEOUT_MS = 9000;
constexpr unsigned long BILL_ROUTE_SETTLE_MS = 3000; // wait after bill detected before routing
constexpr unsigned long ELEVATOR_TO_IR5_1_TIMEOUT_MS = 9000;
constexpr unsigned long ELEVATOR_SLOT_MOVE_TIMEOUT_MS = 12000;
constexpr unsigned long ELEVATOR_SLOT_STOP_MS = 450;
constexpr unsigned long ELEVATOR_CATCH_SETTLE_MS = 120;
constexpr unsigned long ELEVATOR_CATCH_SLOW_UP_MS = 1300;
constexpr unsigned long ELEVATOR_PARK_RECOVER_BURST_MS = 220;
constexpr unsigned long ELEVATOR_PARK_RETRY_HOLD_MS = 350;
constexpr unsigned long ELEVATOR_LIFT_DIRECTION_TEST_MS = 2000;
// Withdrawal timing
constexpr unsigned long WITHDRAW_SPIN_MS = 2500;           // time to eject one bill
constexpr unsigned long WITHDRAW_INTER_BILL_GAP_MS = 600;  // pause between bills
constexpr unsigned long WITHDRAW_TOTAL_TIMEOUT_MS = 120000; // 2-min safety timeout
constexpr unsigned long JOB_MAX_RUNTIME_MS = 900000;        // 15-min motor safety cap

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

struct PulseMap {
  uint8_t pulses;
  float amount;
};

// Update these maps to match the pulse programming in your acceptors.
constexpr PulseMap COIN_MAP[] = {
  // Common CH-926 style programming (1/2/3/4 pulses).
  {1, 1.0f},
  {2, 5.0f},
  {3, 10.0f},
  {4, 20.0f},
  // Alternate pulse scale observed on your coin slot.
  {5, 5.0f},
  {10, 10.0f},
  {15, 15.0f},
  {20, 20.0f},
};

constexpr PulseMap BILL_MAP[] = {
  // Star/default bill acceptor config is commonly 1 pulse per PHP10.
  // With that mode: 20->2 pulses, 50->5, 100->10, 200->20, 500->50, 1000->100.
  {1, 10.0f},
  {2, 20.0f},
  {5, 50.0f},
  {10, 100.0f},
  {20, 200.0f},
  {50, 500.0f},
  {100, 1000.0f},
};

constexpr char WIFI_SSID[] = "VSupreme";
constexpr char WIFI_PASSWORD[] = "Fffggghhh123";
constexpr char DEPOSIT_API_URL[] = "https://cash-app-production-458e.up.railway.app/api/deposit";
constexpr char COMMAND_API_URL[] = "https://cash-app-production-458e.up.railway.app/api/command";
constexpr unsigned long COMMAND_POLL_MS = 10000;

struct MotorState {
  int sequenceIndex;
};

constexpr unsigned long M4_FORWARD_RUN_MS  = 10000;
constexpr unsigned long M4_BACKWARD_RUN_MS = 10000;

struct LocalDispenseJob {
  bool active;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  bool goingForward;
  unsigned long startedMs;
  unsigned long coinDeadlineMs;
  unsigned long phaseStartedMs;
  unsigned long detectStartedMs;
  unsigned long lastStepUs;
};

struct QueueTask {
  uint8_t motor;
  uint8_t count;
};

struct PulseInput {
  uint8_t pin;
  const char* name;
  bool lastLevel;
  bool pending;
  uint8_t pulseCount;
  unsigned long lastEdgeMs;
  unsigned long idleGapMs;
  bool pulseActive;
  unsigned long pulseStartedMs;
};

struct InputDiag {
  bool active;
  uint8_t pin;
  const char* name;
  bool lastLevel;
  uint16_t risingEdges;
  uint16_t fallingEdges;
  unsigned long startedMs;
  unsigned long durationMs;
};

enum BillRouteStage {
  ROUTE_IDLE,
  ROUTE_FIND_LEVEL,
  ROUTE_NUDGE_ABOVE,
  ROUTE_TRANSFER_OUT,
  ROUTE_SPINNER_PULSE,
  ROUTE_RETURN_HOME,
};

struct BillRouteJob {
  bool active;
  BillRouteStage stage;
  uint8_t targetSlot;
  unsigned long startedMs;
  unsigned long stageStartedMs;
  bool liftRunning;
};

enum WithdrawStage {
  WD_IDLE,
  WD_MOVE_TO_SLOT,
  WD_SPIN_BILL,
  WD_BILL_GAP,
  WD_MOVE_HOME,
};

struct WithdrawJob {
  bool active;
  uint8_t slotCounts[5];  // [0]=20s [1]=50s [2]=100s [3]=500s [4]=1000s
  uint8_t currentSlot;
  uint8_t billsLeft;
  WithdrawStage stage;
  unsigned long stageStartedMs;
  unsigned long startedMs;
};

enum LiftToIr51Stage {
  LIFT_TO_1_FIND,
  LIFT_TO_1_SLOW_UP,
};

constexpr QueueTask EMPTY_TASK = {0, 0};
QueueTask taskQueue[16] = {};
uint8_t taskCount = 0;

MotorState motor4 = {};
LocalDispenseJob motor4Job = {};
PulseInput coinInput = {COIN_PIN, "coin", true, false, 0, 0, 250, false, 0};
// Slow pulse mode can stretch high-time to ~300ms between falling edges.
// Use a larger idle gap so a single bill's pulse train is grouped correctly.
PulseInput billInput = {BILL_PIN, "bill", true, false, 0, 0, 900, false, 0};
BillRouteJob billRoute = {};
WithdrawJob withdrawJob = {};
bool liftToIr51Active = false;
LiftToIr51Stage liftToIr51Stage = LIFT_TO_1_FIND;
unsigned long liftToIr51StartedMs = 0;
unsigned long liftToIr51StageStartedMs = 0;
bool liftToIr51SlowUpRunning = false;
bool elevatorParkActive = false;
bool elevatorParkRecovering = false;
uint8_t elevatorParkSlot = 0;
unsigned long elevatorParkRecoverStartedMs = 0;
unsigned long elevatorParkRetryAfterMs = 0;
uint8_t pendingBillSlots[8] = {0};
uint8_t pendingBillCount = 0;
unsigned long billRouteReadyAfterMs = 0;

bool ir5LastState[5] = {false, false, false, false, false};
unsigned long ir5DetectStartedMs[5] = {0, 0, 0, 0, 0};
bool unoOnline = false;
bool remoteTaskActive = false;
bool unoIr4IsLow = false;  // A3 level reported by Uno via IR4_EDGE
bool motionArmed = MOTION_ARMED_DEFAULT;
bool pulseDebugEnabled = false;
InputDiag inputDiag = {false, 0, nullptr, true, 0, 0, 0, 0};
uint8_t currentLiftCommand = SERVO_STOP_CMD;
String usbCommandBuffer;
String unoLineBuffer;
unsigned long lastStatusPrintMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastCommandPollMs = 0;

HardwareSerial UnoSerial(2);
uint8_t servoHomeAngles[SERVO_CHANNEL_COUNT] = {
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
  SERVO_STOP_CMD,
};

void stopElevatorPark();
void startElevatorPark(uint8_t slot);
void handleUsbCommand(String command);

void pcaWriteRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCA9685_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pcaReadRegister(uint8_t reg) {
  Wire.beginTransmission(PCA9685_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(static_cast<int>(PCA9685_ADDRESS), 1);
  return Wire.available() ? Wire.read() : 0;
}

void pcaSetPwm(uint8_t channel, uint16_t onCount, uint16_t offCount) {
  const uint8_t base = PCA9685_LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA9685_ADDRESS);
  Wire.write(base);
  Wire.write(static_cast<uint8_t>(onCount & 0xFF));
  Wire.write(static_cast<uint8_t>(onCount >> 8));
  Wire.write(static_cast<uint8_t>(offCount & 0xFF));
  Wire.write(static_cast<uint8_t>(offCount >> 8));
  Wire.endTransmission();
}

void pcaInit50Hz() {
  pcaWriteRegister(PCA9685_MODE1, 0x10);
  pcaWriteRegister(PCA9685_PRESCALE, 121);
  pcaWriteRegister(PCA9685_MODE1, 0x20);
  delay(5);
  pcaWriteRegister(PCA9685_MODE1, pcaReadRegister(PCA9685_MODE1) | 0xA1);
}

void pcaAllChannelsOff() {
  for (uint8_t channel = 0; channel < 16; ++channel) {
    pcaSetPwm(channel, 0, 0);
  }
}

bool isDetected(int rawValue) {
  return ACTIVE_LOW ? (rawValue == LOW) : (rawValue == HIGH);
}

void writeMotor4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  digitalWrite(M4_IN1_PIN, a);
  digitalWrite(M4_IN2_PIN, c);
  digitalWrite(M4_IN3_PIN, b);
  digitalWrite(M4_IN4_PIN, d);
}

void releaseMotor4() {
  writeMotor4(0, 0, 0, 0);
}

void stepMotor4Forward() {
  writeMotor4(
    HALF_STEP[motor4.sequenceIndex][0],
    HALF_STEP[motor4.sequenceIndex][1],
    HALF_STEP[motor4.sequenceIndex][2],
    HALF_STEP[motor4.sequenceIndex][3]
  );
  motor4.sequenceIndex = (motor4.sequenceIndex + 7) & 0x07;
}

void stepMotor4Backward() {
  writeMotor4(
    HALF_STEP[motor4.sequenceIndex][0],
    HALF_STEP[motor4.sequenceIndex][1],
    HALF_STEP[motor4.sequenceIndex][2],
    HALF_STEP[motor4.sequenceIndex][3]
  );
  motor4.sequenceIndex = (motor4.sequenceIndex + 1) & 0x07;
}

float mapPulseCount(const PulseMap* table, size_t length, uint8_t pulses) {
  for (size_t index = 0; index < length; ++index) {
    if (table[index].pulses == pulses) {
      return table[index].amount;
    }
  }
  return 0.0f;
}

bool ensureWifi() {
  if (WIFI_SSID[0] == '\0' || DEPOSIT_API_URL[0] == '\0') {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  const unsigned long nowMs = millis();
  if (nowMs - lastWifiAttemptMs < 5000) {
    return false;
  }
  lastWifiAttemptMs = nowMs;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println(F("Connecting to WiFi..."));

  const unsigned long startedMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedMs < 5000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected: "));
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println(F("WiFi not connected"));
  return false;
}

void postDeposit(float amount, const char* source, uint8_t pulses) {
  Serial.print(F("DEPOSIT:"));
  Serial.println(amount, 2);
  Serial.print(F("EVENT source="));
  Serial.print(source);
  Serial.print(F(" pulses="));
  Serial.print(pulses);
  Serial.print(F(" amount="));
  Serial.println(amount, 2);

  if (!ensureWifi()) {
    Serial.println(F("ERR: WiFi not connected"));
    return;
  }

  Serial.print(F("WiFi status: "));
  Serial.print(WiFi.status());
  Serial.print(F(" IP: "));
  Serial.println(WiFi.localIP());

  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // skip cert verification

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  
  Serial.print(F("POSTing to: "));
  Serial.println(DEPOSIT_API_URL);
  
  if (!http.begin(secureClient, DEPOSIT_API_URL)) {
    Serial.println(F("ERR: http.begin() failed"));
    http.end();
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  const String body = String("{\"amount\":") + String(amount, 2) + "}";
  
  Serial.print(F("POST body: "));
  Serial.println(body);
  
  const int status = http.POST(body);
  Serial.print(F("HTTP deposit status="));
  Serial.println(status);
  
  if (status < 0) {
    Serial.print(F("ERR: HTTP error: "));
    Serial.println(http.errorToString(status));
  } else if (status == 200) {
    Serial.println(F("OK: deposit received by server"));
  }
  
  http.end();
}

void pollRemoteCommands() {
  if (COMMAND_API_URL[0] == '\0') {
    return;
  }

  // Never block pulse-sensitive paths while acceptor pulses are in progress.
  if (coinInput.pulseActive || billInput.pulseActive || coinInput.pending || billInput.pending) {
    return;
  }

  // Avoid extra network activity while local motion tasks are active.
  if (motor4Job.active || withdrawJob.active || billRoute.active) {
    return;
  }

  if (!ensureWifi()) {
    return;
  }

  const unsigned long nowMs = millis();
  if (nowMs - lastCommandPollMs < COMMAND_POLL_MS) {
    return;
  }
  lastCommandPollMs = nowMs;

  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // skip cert verification

  HTTPClient http;
  http.setConnectTimeout(250);
  http.setTimeout(250);

  String url = String(COMMAND_API_URL);
  if (url.indexOf('?') >= 0) {
    url += "&consume=true";
  } else {
    url += "?consume=true";
  }

  if (!http.begin(secureClient, url)) {
    Serial.println(F("ERR: command http.begin() failed"));
    http.end();
    return;
  }

  const int status = http.GET();
  if (status != 200) {
    if (status < 0) {
      Serial.print(F("ERR: command HTTP error: "));
      Serial.println(http.errorToString(status));
    }
    http.end();
    return;
  }

  const String payload = http.getString();
  http.end();

  const int keyIndex = payload.indexOf("\"commands\"");
  if (keyIndex < 0) {
    return;
  }
  const int listStart = payload.indexOf('[', keyIndex);
  const int listEnd = payload.indexOf(']', listStart);
  if (listStart < 0 || listEnd < 0 || listEnd <= listStart) {
    return;
  }

  int scan = listStart + 1;
  while (scan < listEnd) {
    const int open = payload.indexOf('"', scan);
    if (open < 0 || open >= listEnd) {
      break;
    }

    String cmd;
    bool escaping = false;
    int close = open + 1;
    for (; close < listEnd; ++close) {
      const char ch = payload.charAt(close);
      if (escaping) {
        cmd += ch;
        escaping = false;
        continue;
      }
      if (ch == '\\') {
        escaping = true;
        continue;
      }
      if (ch == '"') {
        break;
      }
      cmd += ch;
    }

    if (close >= listEnd) {
      break;
    }

    cmd.trim();
    if (cmd.length() > 0) {
      Serial.print(F("REMOTE CMD: "));
      Serial.println(cmd);
      handleUsbCommand(cmd);
    }

    scan = close + 1;
  }
}

void setServoAngle(uint8_t channel, uint8_t angle) {
  if (!SERVOS_ENABLED) {
    return;
  }
  if (!motionArmed) {
    return;
  }
  if (channel >= 16) {
    return;
  }
  if (angle > 180) {
    angle = 180;
  }
  const uint16_t pulse = SERVO_PWM_MIN + ((SERVO_PWM_MAX - SERVO_PWM_MIN) * angle) / 180;
  pcaSetPwm(channel, 0, pulse);
}

void homeServos() {
  if (!SERVOS_ENABLED) {
    pcaAllChannelsOff();
    Serial.println(F("ERR servos-disabled"));
    return;
  }

  pcaAllChannelsOff();
  Serial.println(F("OK HOME"));
}

uint8_t billSlotFromPulses(uint8_t pulses) {
  // Map bill pulse count to storage slot (IR5-1..IR5-5 / CH0..CH4).
  if (pulses <= 1) return 0;
  if (pulses == 2) return 1;
  if (pulses == 3) return 2;
  if (pulses == 4) return 3;
  return 4;
}

bool billSlotFromAmount(float amount, uint8_t& slotOut) {
  const int pesos = static_cast<int>(amount + 0.5f);
  switch (pesos) {
    case 20:   slotOut = 0; return true; // IR5-1
    case 50:   slotOut = 1; return true; // IR5-2
    case 100:  slotOut = 2; return true; // IR5-3
    case 500:  slotOut = 3; return true; // IR5-4
    case 1000: slotOut = 4; return true; // IR5-5
    default:
      return false;
  }
}

void queueBillSlot(uint8_t slot) {
  if (!motionArmed) {
    Serial.println(F("WARN bill route ignored (disarmed)"));
    return;
  }
  if (slot > 4) {
    return;
  }
  if (pendingBillCount >= 8) {
    Serial.println(F("WARN bill-route queue full"));
    return;
  }
  pendingBillSlots[pendingBillCount++] = slot;
  billRouteReadyAfterMs = millis() + BILL_ROUTE_SETTLE_MS;
  Serial.print(F("BILL ROUTE QUEUED slot="));
  Serial.print(slot + 1);
  Serial.print(F(" settleMs="));
  Serial.println(BILL_ROUTE_SETTLE_MS);
}

void popBillSlot() {
  if (pendingBillCount == 0) {
    return;
  }
  for (uint8_t i = 1; i < pendingBillCount; ++i) {
    pendingBillSlots[i - 1] = pendingBillSlots[i];
  }
  pendingBillSlots[pendingBillCount - 1] = 0;
  --pendingBillCount;
}

bool isIr5Detected(uint8_t slot) {
  if (slot > 4) {
    return false;
  }
  return isDetected(digitalRead(IR5_PINS[slot]));
}

int8_t currentIr5SlotDetected() {
  for (uint8_t slot = 0; slot < 5; ++slot) {
    if (isIr5Detected(slot)) {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

uint8_t mapLiftCommand(uint8_t logicalCommand) {
  if (!LIFT_DIRECTION_INVERTED || logicalCommand == SERVO_STOP_CMD) {
    return logicalCommand;
  }
  return static_cast<uint8_t>(180 - logicalCommand);
}

bool isLiftUpCommand(uint8_t physicalCommand) {
  return LIFT_DIRECTION_INVERTED
    ? (physicalCommand < SERVO_STOP_CMD)
    : (physicalCommand > SERVO_STOP_CMD);
}

bool isLiftDownCommand(uint8_t physicalCommand) {
  return LIFT_DIRECTION_INVERTED
    ? (physicalCommand > SERVO_STOP_CMD)
    : (physicalCommand < SERVO_STOP_CMD);
}

uint8_t clampLiftCommandToLimits(uint8_t physicalCommand) {
  if (isLiftUpCommand(physicalCommand) && isIr5Detected(0)) {
    return SERVO_STOP_CMD;
  }
  if (isLiftDownCommand(physicalCommand) && isIr5Detected(4)) {
    return SERVO_STOP_CMD;
  }
  return physicalCommand;
}

void setLiftCommand(uint8_t command) {
  const uint8_t physicalCommand = mapLiftCommand(command);
  const uint8_t safeCommand = clampLiftCommandToLimits(physicalCommand);
  currentLiftCommand = safeCommand;
  setServoAngle(ELEVATOR_LIFT_CHANNEL, safeCommand);
}

void serviceLiftHardLimits() {
  if (!SERVOS_ENABLED || !motionArmed) {
    return;
  }

  const uint8_t safeCommand = clampLiftCommandToLimits(currentLiftCommand);
  if (safeCommand != currentLiftCommand) {
    currentLiftCommand = safeCommand;
    setServoAngle(ELEVATOR_LIFT_CHANNEL, safeCommand);
    Serial.println(F("LIFT LIMIT stop"));
  }
}

bool isIr5StableDetected(uint8_t slot, unsigned long nowMs) {
  if (slot > 4) {
    return false;
  }

  if (isIr5Detected(slot)) {
    if (ir5DetectStartedMs[slot] == 0) {
      ir5DetectStartedMs[slot] = nowMs;
      return false;
    }
    return (nowMs - ir5DetectStartedMs[slot]) >= IR5_DETECT_HOLD_MS;
  }

  ir5DetectStartedMs[slot] = 0;
  return false;
}

void startBillRouteIfPending() {
  // For bill input flow, always finish catch-to-IR5-1 first before routing.
  if (billRoute.active || pendingBillCount == 0 || liftToIr51Active || withdrawJob.active) {
    return;
  }
  // Wait for bill settle delay before routing.
  if (billRouteReadyAfterMs > 0 && millis() < billRouteReadyAfterMs) {
    return;
  }

  // Release park hold before route control takes over CH5.
  if (elevatorParkActive) {
    stopElevatorPark();
  }

  billRoute.active = true;
  billRoute.stage = ROUTE_FIND_LEVEL;
  billRoute.targetSlot = pendingBillSlots[0];
  billRoute.startedMs = millis();
  billRoute.stageStartedMs = billRoute.startedMs;
  billRoute.liftRunning = true;

  const int8_t currentSlot = currentIr5SlotDetected();
  if (currentSlot >= 0) {
    if (currentSlot < static_cast<int8_t>(billRoute.targetSlot)) {
      setLiftCommand(ELEVATOR_LIFT_DOWN_CMD);
    } else if (currentSlot > static_cast<int8_t>(billRoute.targetSlot)) {
      setLiftCommand(ELEVATOR_LIFT_UP_CMD);
    } else {
      setLiftCommand(ELEVATOR_LIFT_HOLD_CMD);
      billRoute.liftRunning = false;
    }
  } else {
    // Fallback if no IR5 is currently active.
    setLiftCommand(ELEVATOR_LIFT_UP_CMD);
  }

  Serial.print(F("BILL ROUTE START slot="));
  Serial.println(billRoute.targetSlot + 1);
}

void stopElevatorOutMotor() {
  if (!SERVOS_ENABLED) {
    return;
  }
  // Fully disable CH6 PWM so the continuous servo cannot creep.
  pcaSetPwm(ELEVATOR_OUT_CHANNEL, 0, 0);
}

void stopStorageMotor(uint8_t slot) {
  if (!SERVOS_ENABLED) {
    return;
  }
  if (slot > 4) {
    return;
  }
  // Fully disable slot channel PWM to guarantee no residual spin.
  pcaSetPwm(SPINNER_CHANNEL_MIN + slot, 0, 0);
}

void finishBillRoute(const __FlashStringHelper* result) {
  setLiftCommand(SERVO_STOP_CMD);
  stopElevatorOutMotor();
  stopStorageMotor(billRoute.targetSlot);
  Serial.print(F("BILL ROUTE "));
  Serial.println(result);
  popBillSlot();
  billRoute = {};
}

void serviceBillRoute() {
  if (!billRoute.active) {
    return;
  }

  const unsigned long nowMs = millis();
  if (nowMs - billRoute.startedMs > ELEVATOR_ROUTE_TIMEOUT_MS) {
    finishBillRoute(F("TIMEOUT"));
    return;
  }

  switch (billRoute.stage) {
    case ROUTE_FIND_LEVEL: {
      if (isIr5StableDetected(billRoute.targetSlot, nowMs)) {
        setLiftCommand(ELEVATOR_LIFT_HOLD_CMD);
        billRoute.liftRunning = false;
        billRoute.stage = ROUTE_TRANSFER_OUT;
        billRoute.stageStartedMs = nowMs;
        // Start CH6 only after elevator reaches the target IR location.
        setServoAngle(ELEVATOR_OUT_CHANNEL, ELEVATOR_OUT_PUSH_CMD);
        // Assist feed into storage with the destination slot motor.
        setServoAngle(SPINNER_CHANNEL_MIN + billRoute.targetSlot, SPINNER_RUN_CMD);
        break;
      }
      break;
    }

    case ROUTE_NUDGE_ABOVE: {
      // Unused in current flow; retained for compatibility.
      billRoute.stage = ROUTE_TRANSFER_OUT;
      billRoute.stageStartedMs = nowMs;
      break;
    }

    case ROUTE_TRANSFER_OUT: {
      if (nowMs - billRoute.stageStartedMs >= ELEVATOR_OUT_PULSE_MS) {
        stopElevatorOutMotor();
        stopStorageMotor(billRoute.targetSlot);
        billRoute.stage = ROUTE_RETURN_HOME;
        billRoute.stageStartedMs = nowMs;
        // After transfer, always home back to IR5-1 before waiting for next bill.
        setLiftCommand(ELEVATOR_LIFT_UP_CMD);
      }
      break;
    }

    case ROUTE_SPINNER_PULSE: {
      // Unused in current flow; retained for compatibility.
      billRoute.stage = ROUTE_RETURN_HOME;
      billRoute.stageStartedMs = nowMs;
      setLiftCommand(ELEVATOR_LIFT_DOWN_CMD);
      break;
    }

    case ROUTE_RETURN_HOME: {
      if (isIr5StableDetected(0, nowMs)) {
        setLiftCommand(SERVO_STOP_CMD);
        startElevatorPark(0);
        finishBillRoute(F("DONE"));
      }
      break;
    }

    default:
      break;
  }
}

void queueTask(uint8_t motor, uint8_t count) {
  if (!motionArmed) {
    Serial.println(F("ERR motion-disarmed"));
    return;
  }
  if (!STEPPERS_ENABLED) {
    Serial.println(F("ERR motors-disabled"));
    return;
  }

  if (motor < 1 || motor > 4 || count == 0) {
    Serial.println(F("ERR invalid-task"));
    return;
  }
  if (taskCount >= 16) {
    Serial.println(F("ERR queue-full"));
    return;
  }
  taskQueue[taskCount++] = {motor, count};
  Serial.print(F("QUEUED motor="));
  Serial.print(motor);
  Serial.print(F(" count="));
  Serial.println(count);
}

void popQueueFront() {
  if (taskCount == 0) {
    return;
  }
  for (uint8_t index = 1; index < taskCount; ++index) {
    taskQueue[index - 1] = taskQueue[index];
  }
  taskQueue[taskCount - 1] = EMPTY_TASK;
  --taskCount;
}

void startLocalMotor4(uint8_t count) {
  motor4Job.active = true;
  motor4Job.requestedCount = count;
  motor4Job.dispensedCount = 0;
  motor4Job.sensorArmed = !isDetected(unoIr4IsLow ? LOW : HIGH);
  motor4Job.goingForward = false;
  motor4Job.startedMs = millis();
  motor4Job.coinDeadlineMs = motor4Job.startedMs + JOB_MAX_RUNTIME_MS;
  motor4Job.phaseStartedMs = motor4Job.startedMs;
  motor4Job.detectStartedMs = 0;
  motor4Job.lastStepUs = micros();
  Serial.print(F("OK DISPENSE motor=4 count="));
  Serial.println(count);
  Serial.println(F("M4 PHASE=BWD"));
}

void startNextQueuedTask() {
  if (!STEPPERS_ENABLED) {
    if (taskCount > 0) {
      Serial.println(F("WARN dropping queued motor task (motors-disabled)"));
      popQueueFront();
    }
    return;
  }

  if (taskCount == 0 || remoteTaskActive || motor4Job.active) {
    return;
  }

  const QueueTask nextTask = taskQueue[0];
  if (nextTask.motor == 4) {
    startLocalMotor4(nextTask.count);
    return;
  }

  UnoSerial.print(F("DISPENSE "));
  UnoSerial.print(nextTask.motor);
  UnoSerial.print(' ');
  UnoSerial.println(nextTask.count);
  remoteTaskActive = true;
}

void stopAllDispense() {
  taskCount = 0;
  motor4Job = {};
  remoteTaskActive = false;
  releaseMotor4();
  UnoSerial.println(F("STOP"));
  Serial.println(F("OK STOP"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  ARM"));
  Serial.println(F("  DISARM"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  PINGUNO"));
  Serial.println(F("  DISPENSE <motor 1..4> <count>"));
  Serial.println(F("  PAYOUT <amount>"));
  Serial.println(F("  WITHDRAW <amount>  (20/50/100/500/1000 denominations)"));
  Serial.println(F("  ROUTE <slot1to5>"));
  Serial.println(F("  LIFTUP"));
  Serial.println(F("  LIFTDOWN"));
  Serial.println(F("  LIFTSTOP"));
  Serial.println(F("  LIFTTO1"));
  Serial.println(F("  IR5-1 .. IR5-5"));
  Serial.println(F("  LIFTTEST (IR5-1 -> IR5-5 sequence)"));
  Serial.println(F("  PULSEDEBUG"));
  Serial.println(F("  DIAGBILL"));
  Serial.println(F("  DIAGCOIN"));
  Serial.println(F("  SERVO <channel> <angle>"));
  Serial.println(F("  HOME"));
  Serial.println(F("  STOP"));
  Serial.println(F("  HELP"));
}

void startInputDiag(uint8_t pin, const char* name, unsigned long durationMs) {
  inputDiag.active = true;
  inputDiag.pin = pin;
  inputDiag.name = name;
  inputDiag.lastLevel = (digitalRead(pin) == HIGH);
  inputDiag.risingEdges = 0;
  inputDiag.fallingEdges = 0;
  inputDiag.startedMs = millis();
  inputDiag.durationMs = durationMs;

  Serial.print(F("DIAG start source="));
  Serial.print(name);
  Serial.print(F(" pin="));
  Serial.print(pin);
  Serial.print(F(" idle="));
  Serial.print(inputDiag.lastLevel ? F("HIGH") : F("LOW"));
  Serial.print(F(" windowMs="));
  Serial.println(durationMs);
}

void serviceInputDiag() {
  if (!inputDiag.active) {
    return;
  }

  const bool level = (digitalRead(inputDiag.pin) == HIGH);
  if (level != inputDiag.lastLevel) {
    if (level) {
      ++inputDiag.risingEdges;
    } else {
      ++inputDiag.fallingEdges;
    }
    inputDiag.lastLevel = level;

    Serial.print(F("DIAG edge source="));
    Serial.print(inputDiag.name);
    Serial.print(F(" level="));
    Serial.print(level ? F("HIGH") : F("LOW"));
    Serial.print(F(" rise="));
    Serial.print(inputDiag.risingEdges);
    Serial.print(F(" fall="));
    Serial.println(inputDiag.fallingEdges);
  }

  const unsigned long elapsedMs = millis() - inputDiag.startedMs;
  if (elapsedMs < inputDiag.durationMs) {
    return;
  }

  Serial.print(F("DIAG done source="));
  Serial.print(inputDiag.name);
  Serial.print(F(" idleEnd="));
  Serial.print(inputDiag.lastLevel ? F("HIGH") : F("LOW"));
  Serial.print(F(" rise="));
  Serial.print(inputDiag.risingEdges);
  Serial.print(F(" fall="));
  Serial.println(inputDiag.fallingEdges);

  if (inputDiag.risingEdges == 0 && inputDiag.fallingEdges == 0) {
    Serial.println(F("DIAG result=no pin movement detected"));
  } else if (inputDiag.fallingEdges > 0) {
    Serial.println(F("DIAG result=active-low pulses detected"));
  } else {
    Serial.println(F("DIAG result=pin moved but no falling pulse detected"));
  }

  inputDiag.active = false;
}

uint8_t getElevatorHoldCommand(uint8_t slot) {
  // No hold speed needed at IR5-4 and IR5-5 (bottom slots)
  if (slot == 3 || slot == 4) {
    return SERVO_STOP_CMD;  // 0 hold (full stop)
  }
  return ELEVATOR_LIFT_HOLD_CMD;  // Normal hold for IR5-1, IR5-2, IR5-3
}

int8_t currentIr5Slot() {
  for (uint8_t slot = 0; slot < 5; ++slot) {
    if (isIr5Detected(slot)) {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

bool validateLiftDirection(uint8_t testCommand) {
  if (testCommand == SERVO_STOP_CMD) {
    return true;  // Stop commands don't need validation
  }

  const int8_t positionBefore = currentIr5Slot();
  setLiftCommand(testCommand);
  delay(ELEVATOR_LIFT_DIRECTION_TEST_MS);
  const int8_t positionAfter = currentIr5Slot();
  setLiftCommand(SERVO_STOP_CMD);

  // Check if position changed in the EXPECTED direction
  // UP command (122): slot should DECREASE (4→3→2→1→0)
  // DOWN command (60): slot should INCREASE (0→1→2→3→4)
  bool isUpCommand = testCommand > SERVO_STOP_CMD;
  bool movedUp = positionAfter < positionBefore;
  bool movedDown = positionAfter > positionBefore;

  if (isUpCommand && movedUp) {
    return true;  // Moving UP succeeded
  }
  if (!isUpCommand && movedDown) {
    return true;  // Moving DOWN succeeded
  }

  if (positionBefore >= 0 && positionBefore == positionAfter) {
    Serial.println(F("WARN lift direction test: already at limit (no position change)"));
  } else if (positionBefore >= 0) {
    Serial.print(F("WARN lift direction test: moved opposite direction ("));
    Serial.print(positionBefore);
    Serial.print(F("→"));
    Serial.print(positionAfter);
    Serial.println(F(")"));
  }
  return false;  // Direction blocked or reversed
}

bool moveLiftToIr5Slot(uint8_t slot, uint8_t moveCommand, unsigned long timeoutMs) {
  setLiftCommand(moveCommand);
  const unsigned long startMs = millis();

  while (millis() - startMs < timeoutMs) {
    const unsigned long nowMs = millis();
    if (isIr5StableDetected(slot, nowMs)) {
      return true;
    }
    delay(5);
  }

  return false;
}

bool moveLiftDirectToSlot(uint8_t slot) {
  if (slot > 4) {
    return false;
  }

  const int8_t currentSlot = currentIr5Slot();
  if (currentSlot == static_cast<int8_t>(slot)) {
    startElevatorPark(slot);
    Serial.print(F("IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" already"));
    return true;
  }

  uint8_t moveCommand = ELEVATOR_LIFT_DOWN_CMD;
  if (currentSlot >= 0) {
    moveCommand = (slot < static_cast<uint8_t>(currentSlot)) ? ELEVATOR_LIFT_UP_CMD : ELEVATOR_LIFT_DOWN_CMD;
  } else if (slot == 0) {
    moveCommand = ELEVATOR_LIFT_UP_CMD;
  }

  // Pre-move direction validation: test for 2 seconds to confirm direction is valid
  Serial.print(F("MOVE IR5-"));
  Serial.println(slot + 1);
  if (!validateLiftDirection(moveCommand)) {
    Serial.print(F("ERR IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" direction invalid (at limit)"));
    return false;
  }

  if (!moveLiftToIr5Slot(slot, moveCommand, ELEVATOR_SLOT_MOVE_TIMEOUT_MS)) {
    setLiftCommand(SERVO_STOP_CMD);
    Serial.print(F("ERR IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" timeout"));
    return false;
  }

  startElevatorPark(slot);
  Serial.print(F("OK IR5-"));
  Serial.println(slot + 1);
  return true;
}

void runLiftTest() {
  if (!SERVOS_ENABLED) {
    Serial.println(F("ERR servos-disabled"));
    return;
  }
  if (!motionArmed) {
    Serial.println(F("ERR motion-disarmed"));
    return;
  }
  if (billRoute.active) {
    Serial.println(F("ERR lift busy (bill route active)"));
    return;
  }

  liftToIr51Active = false;
  stopElevatorPark();

  Serial.println(F("LIFTTEST start IR5-1->IR5-5"));
  Serial.println(F("LIFTTEST moving to IR5-1"));
  if (!moveLiftToIr5Slot(0, ELEVATOR_LIFT_UP_CMD, ELEVATOR_SLOT_MOVE_TIMEOUT_MS)) {
    setLiftCommand(SERVO_STOP_CMD);
    Serial.println(F("LIFTTEST timeout IR5-1, continue downward"));
  } else {
    setLiftCommand(SERVO_STOP_CMD);
    Serial.println(F("LIFTTEST at IR5-1"));
    delay(ELEVATOR_SLOT_STOP_MS);
  }

  for (uint8_t slot = 1; slot < 5; ++slot) {
    Serial.print(F("LIFTTEST moving to IR5-"));
    Serial.println(slot + 1);
    if (!moveLiftToIr5Slot(slot, ELEVATOR_LIFT_DOWN_CMD, ELEVATOR_SLOT_MOVE_TIMEOUT_MS)) {
      setLiftCommand(SERVO_STOP_CMD);
      Serial.print(F("LIFTTEST timeout IR5-"));
      Serial.println(slot + 1);
      return;
    }
    setLiftCommand(SERVO_STOP_CMD);
    Serial.print(F("LIFTTEST at IR5-"));
    Serial.println(slot + 1);
    delay(ELEVATOR_SLOT_STOP_MS);
  }

  startElevatorPark(4);
  Serial.println(F("LIFTTEST done IR5-5"));
}

void printStatus() {
  const int8_t currentSlot = currentIr5Slot();
  Serial.print(F("STATUS uno="));
  Serial.print(unoOnline ? F("online") : F("offline"));
  Serial.print(F(" armed="));
  Serial.print(motionArmed ? F("1") : F("0"));
  Serial.print(F(" queue="));
  Serial.print(taskCount);
  Serial.print(F(" remote="));
  Serial.print(remoteTaskActive ? F("1") : F("0"));
  Serial.print(F(" m4="));
  if (motor4Job.active) {
    Serial.print(motor4Job.dispensedCount);
    Serial.print('/');
    Serial.print(motor4Job.requestedCount);
  } else {
    Serial.print(F("idle"));
  }
  Serial.print(F(" ir4="));
  Serial.print(unoIr4IsLow ? 0 : 1);
  Serial.print(F(" coinPin="));
  Serial.print(digitalRead(COIN_PIN));
  Serial.print(F(" billPin="));
  Serial.print(digitalRead(BILL_PIN));
  Serial.print(F(" ir5pos="));
  if (currentSlot >= 0) {
    Serial.print(currentSlot + 1);
  } else {
    Serial.print(F("none"));
  }
  Serial.print(F(" route="));
  if (billRoute.active) {
    Serial.print(F("slot"));
    Serial.print(billRoute.targetSlot + 1);
    Serial.print(F(" stage="));
    Serial.print(static_cast<int>(billRoute.stage));
  } else {
    Serial.print(F("idle"));
  }
  Serial.print(F(" pendingRoutes="));
  Serial.print(pendingBillCount);
  Serial.print(F(" liftTo1="));
  Serial.print(liftToIr51Active ? F("1") : F("0"));
  Serial.print(F(" park="));
  if (elevatorParkActive) {
    Serial.print(elevatorParkSlot + 1);
    Serial.print(elevatorParkRecovering ? F("R") : F("H"));
  } else {
    Serial.print(F("0"));
  }
  Serial.println();
}

void stopElevatorPark() {
  elevatorParkActive = false;
  elevatorParkRecovering = false;
  elevatorParkRecoverStartedMs = 0;
  elevatorParkRetryAfterMs = 0;
}

void startLiftToIr51Sequence(const __FlashStringHelper* reason) {
  if (!SERVOS_ENABLED || !motionArmed) {
    return;
  }
  if (billRoute.active) {
    return;
  }

  stopElevatorPark();
  liftToIr51Active = true;
  liftToIr51Stage = LIFT_TO_1_FIND;
  liftToIr51StartedMs = millis();
  liftToIr51StageStartedMs = liftToIr51StartedMs;
  liftToIr51SlowUpRunning = false;
  setLiftCommand(ELEVATOR_LIFT_UP_CMD);

  Serial.print(F("LIFTTO1 START "));
  Serial.println(reason);
}

void startElevatorPark(uint8_t slot) {
  if (slot > 4) {
    return;
  }
  elevatorParkActive = true;
  elevatorParkRecovering = false;
  elevatorParkSlot = slot;
  elevatorParkRecoverStartedMs = 0;
  elevatorParkRetryAfterMs = 0;
    if (slot == 3 || slot == 4) {
      currentLiftCommand = SERVO_STOP_CMD;
      pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);  // Fully cut power at IR5-4 and IR5-5
    } else {
      setLiftCommand(getElevatorHoldCommand(slot));
    }
}

void serviceElevatorPark() {
  if (!elevatorParkActive || !motionArmed || billRoute.active || liftToIr51Active) {
    return;
  }

  const unsigned long nowMs = millis();
  if (elevatorParkSlot == 3 || elevatorParkSlot == 4) {
    // Bottom slots: keep lift channel fully off and never run recovery bursts.
    elevatorParkRecovering = false;
    elevatorParkRecoverStartedMs = 0;
    elevatorParkRetryAfterMs = 0;
    currentLiftCommand = SERVO_STOP_CMD;
    pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
    return;
  }

  if (isIr5StableDetected(elevatorParkSlot, nowMs)) {
    if (elevatorParkRecovering) {
      elevatorParkRecovering = false;
      elevatorParkRecoverStartedMs = 0;
      elevatorParkRetryAfterMs = 0;
        if (elevatorParkSlot == 3 || elevatorParkSlot == 4) {
          currentLiftCommand = SERVO_STOP_CMD;
          pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
        } else {
          setLiftCommand(getElevatorHoldCommand(elevatorParkSlot));
        }
      Serial.print(F("ELEVATOR PARK RESTORED slot="));
      Serial.println(elevatorParkSlot + 1);
    }
    return;
  }

  if (elevatorParkRecovering) {
    if (nowMs - elevatorParkRecoverStartedMs >= ELEVATOR_PARK_RECOVER_BURST_MS) {
      elevatorParkRecovering = false;
      elevatorParkRecoverStartedMs = 0;
      elevatorParkRetryAfterMs = nowMs + ELEVATOR_PARK_RETRY_HOLD_MS;
        if (elevatorParkSlot == 3 || elevatorParkSlot == 4) {
          currentLiftCommand = SERVO_STOP_CMD;
          pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
        } else {
          setLiftCommand(getElevatorHoldCommand(elevatorParkSlot));
        }
    }
    return;
  }

  if (nowMs < elevatorParkRetryAfterMs) {
    return;
  }

  if (!elevatorParkRecovering) {
    elevatorParkRecovering = true;
    elevatorParkRecoverStartedMs = nowMs;
    setLiftCommand(ELEVATOR_LIFT_RECOVER_CMD);
    Serial.print(F("ELEVATOR PARK RECOVER slot="));
    Serial.println(elevatorParkSlot + 1);
  }
}

void updateElevatorParkHold() {
  if (elevatorParkActive && !elevatorParkRecovering) {
      if (elevatorParkSlot == 3 || elevatorParkSlot == 4) {
        currentLiftCommand = SERVO_STOP_CMD;
        pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
      } else {
        uint8_t holdCmd = getElevatorHoldCommand(elevatorParkSlot);
        if (holdCmd != currentLiftCommand) {
          setLiftCommand(holdCmd);
        }
      }
  }
}

void stopLiftToIr51(const __FlashStringHelper* result) {
  setLiftCommand(SERVO_STOP_CMD);
  liftToIr51Active = false;
  liftToIr51SlowUpRunning = false;
  liftToIr51StartedMs = 0;
  liftToIr51StageStartedMs = 0;
  Serial.print(F("LIFTTO1 "));
  Serial.println(result);
}

void serviceLiftToIr51() {
  if (!liftToIr51Active) {
    return;
  }

  const unsigned long nowMs = millis();
  if (nowMs - liftToIr51StartedMs > ELEVATOR_TO_IR5_1_TIMEOUT_MS) {
    stopLiftToIr51(F("TIMEOUT"));
    return;
  }

  switch (liftToIr51Stage) {
    case LIFT_TO_1_FIND: {
      if (isIr5StableDetected(0, nowMs)) {
        setLiftCommand(SERVO_STOP_CMD);
        liftToIr51Stage = LIFT_TO_1_SLOW_UP;
        liftToIr51StageStartedMs = nowMs;
        liftToIr51SlowUpRunning = false;
        Serial.println(F("LIFTTO1 AT_IR5_1"));
      }
      break;
    }

    case LIFT_TO_1_SLOW_UP: {
      const unsigned long elapsedMs = nowMs - liftToIr51StageStartedMs;
      if (!liftToIr51SlowUpRunning && elapsedMs >= ELEVATOR_CATCH_SETTLE_MS) {
        liftToIr51SlowUpRunning = true;
        setLiftCommand(ELEVATOR_LIFT_CATCH_SLOW_UP_CMD);
        Serial.println(F("LIFTTO1 SLOW_UP"));
      }

      if (elapsedMs >= (ELEVATOR_CATCH_SETTLE_MS + ELEVATOR_CATCH_SLOW_UP_MS)) {
        startElevatorPark(0);
        liftToIr51Active = false;
        liftToIr51SlowUpRunning = false;
        liftToIr51StartedMs = 0;
        liftToIr51StageStartedMs = 0;
        Serial.println(F("LIFTTO1 HOLD"));
        updateElevatorParkHold();
      }
      break;
    }

    default:
      break;
  }
}

void finalizePulseInput(PulseInput& input, const PulseMap* map, size_t length) {
  const float amount = mapPulseCount(map, length, input.pulseCount);
  if (amount > 0.0f) {
    postDeposit(amount, input.name, input.pulseCount);
    if (input.name[0] == 'b') {
      if (motionArmed && SERVOS_ENABLED) {
        startLiftToIr51Sequence(F("BILL"));
      }
      uint8_t slot = 0;
      if (billSlotFromAmount(amount, slot)) {
        queueBillSlot(slot);
      } else {
        Serial.print(F("WARN no IR5 slot for bill amount="));
        Serial.println(amount, 2);
      }
    }
  } else {
    Serial.print(F("WARN unmatched pulses source="));
    Serial.print(input.name);
    Serial.print(F(" pulses="));
    Serial.println(input.pulseCount);
  }
  input.pending = false;
  input.pulseCount = 0;
}

void servicePulseInput(PulseInput& input, const PulseMap* map, size_t length) {
  const bool level = (digitalRead(input.pin) == HIGH);
  const unsigned long nowMs = millis();

  if (pulseDebugEnabled && level != input.lastLevel) {
    Serial.print(F("PULSEDBG source="));
    Serial.print(input.name);
    Serial.print(F(" pin="));
    Serial.print(input.pin);
    Serial.print(F(" level="));
    Serial.println(level ? F("HIGH") : F("LOW"));
  }

  const bool pulseStartEdge = PULSE_ACTIVE_LOW
                                  ? (input.lastLevel && !level)
                                  : (!input.lastLevel && level);
  const bool pulseEndEdge = PULSE_ACTIVE_LOW
                                ? (!input.lastLevel && level)
                                : (input.lastLevel && !level);

  if (pulseStartEdge) {
    input.pulseActive = true;
    input.pulseStartedMs = nowMs;
  }

  if (pulseEndEdge && input.pulseActive) {
    const unsigned long pulseWidthMs = nowMs - input.pulseStartedMs;
    input.pulseActive = false;

    if (pulseWidthMs >= PULSE_MIN_WIDTH_MS &&
        pulseWidthMs <= PULSE_MAX_WIDTH_MS &&
        (nowMs - input.lastEdgeMs) > PULSE_MIN_EDGE_GAP_MS) {
      ++input.pulseCount;
      input.pending = true;
      input.lastEdgeMs = nowMs;
      if (pulseDebugEnabled) {
        Serial.print(F("PULSEDBG edge source="));
        Serial.print(input.name);
        Serial.print(F(" widthMs="));
        Serial.print(pulseWidthMs);
        Serial.print(F(" count="));
        Serial.println(input.pulseCount);
      }
    } else if (pulseDebugEnabled) {
      Serial.print(F("PULSEDBG noise source="));
      Serial.print(input.name);
      Serial.print(F(" widthMs="));
      Serial.println(pulseWidthMs);
    }
  }

  if (input.pulseActive && (nowMs - input.pulseStartedMs) > (PULSE_MAX_WIDTH_MS + 100)) {
    input.pulseActive = false;
  }
  input.lastLevel = level;

  if (input.pending && (nowMs - input.lastEdgeMs) >= input.idleGapMs) {
    finalizePulseInput(input, map, length);
  }
}

void serviceIr5Sensors() {
  for (uint8_t index = 0; index < 5; ++index) {
    const bool detected = isDetected(digitalRead(IR5_PINS[index]));
    if (detected != ir5LastState[index]) {
      ir5LastState[index] = detected;
      if (IR5_VERBOSE_LOG) {
        Serial.print(F("IR5_"));
        Serial.print(index + 1);
        Serial.print('=');
        Serial.println(detected ? F("DETECTED") : F("CLEAR"));
      }
    }
  }
}

void finishLocalMotor4() {
  const uint8_t dispensed = motor4Job.dispensedCount;
  motor4Job = {};
  releaseMotor4();
  Serial.print(F("DONE motor=4 count="));
  Serial.println(dispensed);
  popQueueFront();
}

void failLocalMotor4(const __FlashStringHelper* reason) {
  motor4Job = {};
  releaseMotor4();
  Serial.print(F("ERR motor=4 reason="));
  Serial.println(reason);
  popQueueFront();
}

void serviceLocalMotor4() {
  if (!motor4Job.active) {
    return;
  }

  const unsigned long nowMs = millis();
  const unsigned long nowUs = micros();
  if (nowMs >= motor4Job.coinDeadlineMs) {
    failLocalMotor4(F("timeout"));
    return;
  }

  // Oscillate forward/backward until enough coins are detected.
  const unsigned long phaseRunMs = motor4Job.goingForward ? M4_FORWARD_RUN_MS : M4_BACKWARD_RUN_MS;
  if (nowMs - motor4Job.phaseStartedMs >= phaseRunMs) {
    motor4Job.goingForward = !motor4Job.goingForward;
    motor4Job.phaseStartedMs = nowMs;
    Serial.print(F("M4 PHASE="));
    Serial.println(motor4Job.goingForward ? F("FWD") : F("BWD"));
  }
  if (nowUs - motor4Job.lastStepUs >= 2200) {
    if (motor4Job.goingForward) {
      stepMotor4Forward();
    } else {
      stepMotor4Backward();
    }
    motor4Job.lastStepUs = nowUs;
  }

  const bool detected = isDetected(unoIr4IsLow ? LOW : HIGH);
  if (!motor4Job.sensorArmed) {
    if (!detected) {
      motor4Job.sensorArmed = true;
      motor4Job.detectStartedMs = 0;
    }
    return;
  }

  if (detected) {
    if (motor4Job.detectStartedMs == 0) {
      motor4Job.detectStartedMs = nowMs;
    } else if (nowMs - motor4Job.detectStartedMs >= DETECT_HOLD_MS) {
      ++motor4Job.dispensedCount;
      motor4Job.sensorArmed = false;
      motor4Job.detectStartedMs = 0;
      Serial.print(F("EVENT motor=4 dispensed="));
      Serial.println(motor4Job.dispensedCount);

      if (motor4Job.dispensedCount >= motor4Job.requestedCount) {
        finishLocalMotor4();
      }
    }
  } else {
    motor4Job.detectStartedMs = 0;
  }
}

void handlePayoutCommand(int amount) {
  // Release all storage holds before dispensing cash out.
  for (uint8_t slot = 0; slot < 5; ++slot) {
    stopStorageMotor(slot);
  }

  if (!STEPPERS_ENABLED) {
    Serial.println(F("ERR payout-disabled (motors-disabled)"));
    return;
  }

  if (amount <= 0) {
    Serial.println(F("ERR invalid-amount"));
    return;
  }

  const int denominations[] = {20, 10, 5, 1};
  const uint8_t motors[] = {4, 3, 2, 1};
  int remaining = amount;
  for (uint8_t index = 0; index < 4; ++index) {
    const uint8_t count = static_cast<uint8_t>(remaining / denominations[index]);
    if (count > 0) {
      queueTask(motors[index], count);
      remaining -= count * denominations[index];
    }
  }

  if (remaining != 0) {
    Serial.print(F("WARN payout remainder="));
    Serial.println(remaining);
  }
}

// ---------------------------------------------------------------------------
// Withdrawal state machine – dispenses bills from storage via spinner reverse.
// ---------------------------------------------------------------------------

bool calculateWithdrawBills(int amount, uint8_t counts[5]) {
  // counts[0..4] map to slots 0..4 (PHP20, 50, 100, 500, 1000).
  // Returns the remainder that cannot be covered by bills (0-19).
  for (uint8_t i = 0; i < 5; ++i) counts[i] = 0;
  if (amount <= 0) return false;

  counts[4] = static_cast<uint8_t>(amount / 1000); amount %= 1000;
  counts[3] = static_cast<uint8_t>(amount / 500);  amount %= 500;
  counts[2] = static_cast<uint8_t>(amount / 100);  amount %= 100;
  counts[1] = static_cast<uint8_t>(amount / 50);   amount %= 50;
  counts[0] = static_cast<uint8_t>(amount / 20);   amount %= 20;
  // Remainder (0-19) handled separately by coin payout motors.
  return true;
}

void advanceWithdrawToNextSlot() {
  uint8_t next = withdrawJob.currentSlot + 1;
  while (next < 5 && withdrawJob.slotCounts[next] == 0) {
    ++next;
  }
  if (next >= 5) {
    withdrawJob.active = false;
    withdrawJob.stage = WD_IDLE;
    Serial.println(F("WITHDRAW DONE"));
    return;
  }
  withdrawJob.currentSlot = next;
  withdrawJob.billsLeft = withdrawJob.slotCounts[next];
  withdrawJob.stage = WD_BILL_GAP;
  withdrawJob.stageStartedMs = millis();
  Serial.print(F("WITHDRAW NEXT slot="));
  Serial.println(next + 1);
}

bool startWithdrawJob(int amount) {
  if (withdrawJob.active) { Serial.println(F("ERR withdraw-busy")); return false; }
  if (!motionArmed)        { Serial.println(F("ERR withdraw-disarmed")); return false; }
  if (!SERVOS_ENABLED)     { Serial.println(F("ERR withdraw-servos-disabled")); return false; }
  if (billRoute.active)    { Serial.println(F("ERR withdraw-routing-busy")); return false; }

  uint8_t counts[5] = {};
  if (!calculateWithdrawBills(amount, counts)) {
    Serial.print(F("ERR withdraw-invalid amount="));
    Serial.println(amount);
    return false;
  }

  // Calculate sub-20 remainder to be dispensed as coins via stepper motors.
  const int coinRemainder = amount
    - counts[4] * 1000 - counts[3] * 500
    - counts[2] * 100  - counts[1] * 50
    - counts[0] * 20;
  if (coinRemainder > 0) {
    handlePayoutCommand(coinRemainder);
  }

  if (elevatorParkActive) stopElevatorPark();
  // Withdrawal mode must never drive elevator or CH6 output motor.
  setLiftCommand(SERVO_STOP_CMD);
  stopElevatorOutMotor();

  withdrawJob = {};
  for (uint8_t i = 0; i < 5; ++i) withdrawJob.slotCounts[i] = counts[i];
  withdrawJob.active = true;
  withdrawJob.startedMs = millis();
  withdrawJob.stageStartedMs = withdrawJob.startedMs;

  // Find first slot with bills
  withdrawJob.currentSlot = 0;
  while (withdrawJob.currentSlot < 5 && withdrawJob.slotCounts[withdrawJob.currentSlot] == 0) {
    ++withdrawJob.currentSlot;
  }

  Serial.print(F("WITHDRAW START amount="));
  Serial.print(amount);
  Serial.print(F(" 20x")); Serial.print(counts[0]);
  Serial.print(F(" 50x")); Serial.print(counts[1]);
  Serial.print(F(" 100x")); Serial.print(counts[2]);
  Serial.print(F(" 500x")); Serial.print(counts[3]);
  Serial.print(F(" 1000x")); Serial.print(counts[4]);
  if (coinRemainder > 0) {
    Serial.print(F(" coins="));
    Serial.print(coinRemainder);
  }
  Serial.println();

  withdrawJob.billsLeft = withdrawJob.slotCounts[withdrawJob.currentSlot];
  setServoAngle(SPINNER_CHANNEL_MIN + withdrawJob.currentSlot, SPINNER_WITHDRAW_CMD);
  withdrawJob.stage = WD_SPIN_BILL;
  withdrawJob.stageStartedMs = withdrawJob.startedMs;
  Serial.print(F("WITHDRAW SPIN slot="));
  Serial.print(withdrawJob.currentSlot + 1);
  Serial.print(F(" bills_left=")); Serial.println(withdrawJob.billsLeft);
  return true;
}

void serviceWithdrawJob() {
  if (!withdrawJob.active) return;
  const unsigned long nowMs = millis();

  // Safety: keep elevator and CH6 stopped during withdrawal.
  setLiftCommand(SERVO_STOP_CMD);
  stopElevatorOutMotor();

  if (nowMs - withdrawJob.startedMs > WITHDRAW_TOTAL_TIMEOUT_MS) {
    stopStorageMotor(withdrawJob.currentSlot);
    setLiftCommand(SERVO_STOP_CMD);
    withdrawJob = {};
    Serial.println(F("WITHDRAW ERR timeout"));
    return;
  }

  switch (withdrawJob.stage) {
    case WD_MOVE_TO_SLOT: {
      // Not used in storage-only withdrawal mode.
      withdrawJob.stage = WD_BILL_GAP;
      withdrawJob.stageStartedMs = nowMs;
      break;
    }
    case WD_SPIN_BILL: {
      if (nowMs - withdrawJob.stageStartedMs >= WITHDRAW_SPIN_MS) {
        stopStorageMotor(withdrawJob.currentSlot);
        --withdrawJob.billsLeft;
        Serial.print(F("WITHDRAW EJECTED slot="));
        Serial.print(withdrawJob.currentSlot + 1);
        Serial.print(F(" remaining=")); Serial.println(withdrawJob.billsLeft);
        if (withdrawJob.billsLeft > 0) {
          withdrawJob.stage = WD_BILL_GAP;
          withdrawJob.stageStartedMs = nowMs;
        } else {
          advanceWithdrawToNextSlot();
        }
      }
      break;
    }
    case WD_BILL_GAP: {
      if (nowMs - withdrawJob.stageStartedMs >= WITHDRAW_INTER_BILL_GAP_MS) {
        setServoAngle(SPINNER_CHANNEL_MIN + withdrawJob.currentSlot, SPINNER_WITHDRAW_CMD);
        withdrawJob.stage = WD_SPIN_BILL;
        withdrawJob.stageStartedMs = nowMs;
        Serial.print(F("WITHDRAW SPIN slot="));
        Serial.print(withdrawJob.currentSlot + 1);
        Serial.print(F(" bills_left=")); Serial.println(withdrawJob.billsLeft);
      }
      break;
    }
    case WD_MOVE_HOME: {
      // Not used in storage-only withdrawal mode.
      withdrawJob.active = false;
      withdrawJob.stage = WD_IDLE;
      Serial.println(F("WITHDRAW DONE"));
      break;
    }
    default: break;
  }
}

void handleUsbCommand(String command) {
  command.trim();
  if (command.length() == 0) {
    return;
  }

  if (command == F("STATUS")) {
    printStatus();
    UnoSerial.println(F("STATUS"));
    return;
  }
  if (command == F("ARM")) {
    motionArmed = true;
    pcaAllChannelsOff();
    Serial.println(F("OK ARMED"));
    return;
  }
  if (command == F("DISARM")) {
    motionArmed = false;
    liftToIr51Active = false;
    liftToIr51StartedMs = 0;
    stopElevatorPark();
    stopAllDispense();
    pcaAllChannelsOff();
    Serial.println(F("OK DISARMED"));
    return;
  }
  if (command == F("PINGUNO")) {
    UnoSerial.println(F("PING"));
    return;
  }
  if (command == F("STOP")) {
    stopElevatorPark();
    stopAllDispense();
    return;
  }
  if (command == F("HOME")) {
    stopElevatorPark();
    homeServos();
    return;
  }
  if (command == F("HELP")) {
    printHelp();
    return;
  }
  if (command == F("PULSEDEBUG")) {
    pulseDebugEnabled = !pulseDebugEnabled;
    Serial.print(F("OK PULSEDEBUG="));
    Serial.println(pulseDebugEnabled ? F("1") : F("0"));
    Serial.print(F("RAW billPin="));
    Serial.print(digitalRead(BILL_PIN));
    Serial.print(F(" coinPin="));
    Serial.println(digitalRead(COIN_PIN));
    return;
  }
  if (command == F("DIAGBILL")) {
    startInputDiag(BILL_PIN, "bill", 10000);
    return;
  }
  if (command == F("DIAGCOIN")) {
    startInputDiag(COIN_PIN, "coin", 10000);
    return;
  }

  if (command.startsWith(F("DISPENSE "))) {
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }
    const uint8_t motor = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace).toInt());
    const uint8_t count = static_cast<uint8_t>(command.substring(secondSpace + 1).toInt());
    queueTask(motor, count);
    return;
  }

  if (command.startsWith(F("PAYOUT "))) {
    const int amount = command.substring(7).toInt();
    handlePayoutCommand(amount);
    return;
  }

  if (command.startsWith(F("WITHDRAW "))) {
    const int amount = command.substring(9).toInt();
    startWithdrawJob(amount);
    return;
  }

  if (command.startsWith(F("ROUTE "))) {
    const int slot = command.substring(6).toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR route slot 1..5"));
      return;
    }
    queueBillSlot(static_cast<uint8_t>(slot - 1));
    return;
  }

  if (command == F("LIFTUP")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    if (!validateLiftDirection(ELEVATOR_LIFT_UP_CMD)) {
      Serial.println(F("ERR already at top limit"));
      return;
    }
    setLiftCommand(ELEVATOR_LIFT_UP_CMD);
    Serial.println(F("OK LIFTUP"));
    return;
  }

  if (command == F("LIFTDOWN")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    if (!validateLiftDirection(ELEVATOR_LIFT_DOWN_CMD)) {
      Serial.println(F("ERR already at bottom limit"));
      return;
    }
    setLiftCommand(ELEVATOR_LIFT_DOWN_CMD);
    Serial.println(F("OK LIFTDOWN"));
    return;
  }

  if (command == F("LIFTSTOP")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    setLiftCommand(SERVO_STOP_CMD);
    Serial.println(F("OK LIFTSTOP"));
    return;
  }

  if (command == F("LIFTTO1")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    if (!motionArmed) {
      Serial.println(F("ERR motion-disarmed"));
      return;
    }
    if (billRoute.active) {
      Serial.println(F("ERR lift busy (bill route active)"));
      return;
    }
    startLiftToIr51Sequence(F("CMD"));
    Serial.println(F("OK LIFTTO1"));
    return;
  }

  if (command.startsWith(F("IR5-")) && command.length() == 5) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (IR5)"));
    }
    if (billRoute.active) {
      Serial.println(F("ERR lift busy (bill route active)"));
      return;
    }

    const int slot = command.substring(4).toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR IR5 slot 1..5"));
      return;
    }

    liftToIr51Active = false;
    stopElevatorPark();
    moveLiftDirectToSlot(static_cast<uint8_t>(slot - 1));
    return;
  }

  if (command == F("LIFTTEST")) {
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (LIFTTEST)"));
    }
    runLiftTest();
    return;
  }

  if (command.startsWith(F("SERVO "))) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }
    const uint8_t channel = static_cast<uint8_t>(command.substring(firstSpace + 1, secondSpace).toInt());
    const uint8_t angle = static_cast<uint8_t>(command.substring(secondSpace + 1).toInt());
    if (channel == ELEVATOR_LIFT_CHANNEL) {
      setLiftCommand(angle);
    } else {
      setServoAngle(channel, angle);
    }
    Serial.print(F("OK SERVO channel="));
    Serial.print(channel);
    Serial.print(F(" angle="));
    Serial.println(angle);
    return;
  }

  if (command.startsWith(F("TESTDEPOSIT "))) {
    const float amount = command.substring(12).toFloat();
    if (amount > 0 && amount <= 10000) {
      Serial.print(F("TEST: manually posting deposit "));
      Serial.println(amount, 2);
      postDeposit(amount, "TEST", 1);
      Serial.println(F("OK TESTDEPOSIT"));
    } else {
      Serial.println(F("ERR testdeposit: amount 0.01 to 10000"));
    }
    return;
  }

  Serial.println(F("ERR unknown"));
}

void readUsbCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      handleUsbCommand(usbCommandBuffer);
      usbCommandBuffer = "";
      continue;
    }
    if (usbCommandBuffer.length() < 96) {
      usbCommandBuffer += incoming;
    }
  }
}

void handleUnoLine(const String& line) {
  if (line.length() == 0) {
    return;
  }
  Serial.print(F("UNO> "));
  Serial.println(line);

  if (line == F("PONG")) {
    unoOnline = true;
    return;
  }
  if (line.startsWith(F("STATUS "))) {
    unoOnline = true;
    return;
  }
  if (line == F("IR 20P BLOCK")) {
    unoIr4IsLow = true;   // A3 blocked = coin present (ACTIVE_LOW)
    return;
  }
  if (line == F("IR 20P CLEAR")) {
    unoIr4IsLow = false;
    return;
  }
  if (line.startsWith(F("OK DISPENSE"))) {
    unoOnline = true;
    return;
  }
  if (line.startsWith(F("DONE motor="))) {
    unoOnline = true;
    remoteTaskActive = false;
    if (taskCount > 0 && taskQueue[0].motor >= 1 && taskQueue[0].motor <= 3) {
      popQueueFront();
    }
    return;
  }
  if (line.startsWith(F("ERR motor=")) || line == F("OK STOP")) {
    unoOnline = true;
    remoteTaskActive = false;
    if (taskCount > 0 && taskQueue[0].motor >= 1 && taskQueue[0].motor <= 3) {
      popQueueFront();
    }
  }
}

void readUnoLines() {
  while (UnoSerial.available() > 0) {
    const char incoming = static_cast<char>(UnoSerial.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      handleUnoLine(unoLineBuffer);
      unoLineBuffer = "";
      continue;
    }
    if (unoLineBuffer.length() < 128) {
      unoLineBuffer += incoming;
    }
  }
}

}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  UnoSerial.begin(UNO_SERIAL_BAUD, SERIAL_8N1, UNO_RX_PIN, UNO_TX_PIN);

  pinMode(M4_IN1_PIN, OUTPUT);
  pinMode(M4_IN2_PIN, OUTPUT);
  pinMode(M4_IN3_PIN, OUTPUT);
  pinMode(M4_IN4_PIN, OUTPUT);
  pinMode(IR4_PIN, INPUT);
  pinMode(BILL_PIN, INPUT_PULLUP);
  pinMode(COIN_PIN, INPUT_PULLUP);
  for (uint8_t index = 0; index < 5; ++index) {
    pinMode(IR5_PINS[index], INPUT);
    ir5LastState[index] = isDetected(digitalRead(IR5_PINS[index]));
  }

  releaseMotor4();

  Wire.begin(21, 22);
  pcaInit50Hz();
  if (SERVOS_ENABLED && motionArmed) {
    homeServos();
  } else {
    pcaAllChannelsOff();
  }

  coinInput.lastLevel = (digitalRead(COIN_PIN) == HIGH);
  billInput.lastLevel = (digitalRead(BILL_PIN) == HIGH);
  lastStatusPrintMs = millis();

  Serial.println(F("ESP32 CASH ATM CONTROLLER READY"));
  if (!STEPPERS_ENABLED) {
    Serial.println(F("SAFE MODE: all stepper motors disabled"));
  }
  if (!SERVOS_ENABLED) {
    Serial.println(F("SAFE MODE: all PCA9685 servo outputs disabled"));
  }
  if (!motionArmed) {
    Serial.println(F("SAFE MODE: motion disarmed (use ARM command to enable movement)"));
  }
  if (ACCEPTORS_CONNECTED) {
    Serial.print(F("Pulse input pins: bill=GPIO"));
    Serial.print(BILL_PIN);
    Serial.print(F(" coin=GPIO"));
    Serial.println(COIN_PIN);
    Serial.println(F("Use pull-ups as needed for your selected GPIOs and acceptor output type."));
    Serial.print(F("Pulse inputs: bill="));
    Serial.print(BILL_INPUT_ENABLED ? F("ENABLED") : F("DISABLED"));
    Serial.print(F(" coin="));
    Serial.println(COIN_INPUT_ENABLED ? F("ENABLED") : F("DISABLED"));
  } else {
    Serial.println(F("INFO: bill/coin acceptors disconnected; pulse inputs ignored."));
  }
  printHelp();

  if (AUTO_LIFT_TO_IR5_1_ON_BOOT && SERVOS_ENABLED && !billRoute.active) {
    motionArmed = true;
    pcaAllChannelsOff();
    startLiftToIr51Sequence(F("BOOT"));
    Serial.println(F("AUTO: LIFTTO1 requested"));
  }

  if (AUTO_LIFT_TO_BOTTOM_ON_BOOT && SERVOS_ENABLED && !billRoute.active) {
    motionArmed = true;
    pcaAllChannelsOff();
    Serial.println(F("AUTO: moving to bottom IR5-5"));
    if (moveLiftToIr5Slot(4, ELEVATOR_LIFT_DOWN_CMD, ELEVATOR_SLOT_MOVE_TIMEOUT_MS)) {
      setLiftCommand(SERVO_STOP_CMD);
      Serial.println(F("AUTO: at bottom IR5-5"));
    } else {
      setLiftCommand(SERVO_STOP_CMD);
      Serial.println(F("AUTO: bottom IR5-5 timeout"));
    }
  }

  // Initiate Wi-Fi connection on boot
  if (WIFI_SSID[0] != '\0' && DEPOSIT_API_URL[0] != '\0') {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(F("Connecting to WiFi "));
    Serial.println(WIFI_SSID);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(F("\nWiFi connected: "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("\nWiFi connection timeout"));
    }
  }

  UnoSerial.println(F("PING"));
}

void loop() {
  readUsbCommands();
  readUnoLines();
  serviceInputDiag();

  if (ACCEPTORS_CONNECTED && COIN_INPUT_ENABLED) {
    servicePulseInput(coinInput, COIN_MAP, sizeof(COIN_MAP) / sizeof(COIN_MAP[0]));
  }
  if (ACCEPTORS_CONNECTED && BILL_INPUT_ENABLED) {
    servicePulseInput(billInput, BILL_MAP, sizeof(BILL_MAP) / sizeof(BILL_MAP[0]));
  }
  serviceIr5Sensors();
  if (ACCEPTORS_CONNECTED && AUTO_BILL_ROUTE_ENABLED) {
    startBillRouteIfPending();
    serviceBillRoute();
  }
  serviceLiftToIr51();
  serviceElevatorPark();
  serviceLiftHardLimits();
  serviceLocalMotor4();
  startNextQueuedTask();
  serviceWithdrawJob();
  pollRemoteCommands();

  const unsigned long nowMs = millis();
  if (STATUS_VERBOSE_LOG && (nowMs - lastStatusPrintMs >= STATUS_PRINT_MS)) {
    lastStatusPrintMs = nowMs;
    printStatus();
  }
}