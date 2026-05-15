#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Wire.h>

namespace {

constexpr long USB_SERIAL_BAUD = 115200;
constexpr long UNO_SERIAL_BAUD = 9600;
constexpr uint8_t UNO_RX_PIN = 16;
constexpr uint8_t UNO_TX_PIN = 5;  // moved off GPIO17 (suspect dead) to GPIO5
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

// IR sensors default to active-low.
constexpr bool ACTIVE_LOW = true;
// Coin/bill acceptor outputs are typically open-collector and pull LOW on pulse.
constexpr bool PULSE_ACTIVE_LOW = true;
constexpr unsigned long DETECT_HOLD_MS = 10;
constexpr unsigned long IR5_DETECT_HOLD_IDLE_MS = 90;
constexpr unsigned long IR5_DETECT_HOLD_MOVING_MS = 25;
constexpr unsigned long IR5_EDGE_DEBOUNCE_IDLE_MS = 25;
constexpr unsigned long IR5_EDGE_DEBOUNCE_MOVING_MS = 8;
constexpr unsigned long IR5_SLOT4_DEBOUNCE_IDLE_MS = 120;
constexpr unsigned long IR5_SLOT4_DEBOUNCE_MOVING_MS = 40;
constexpr unsigned long IR5_SLOT4_EXTRA_HOLD_MS = 120;
constexpr bool IR5_RAW_EVENT_LOG = false;
constexpr unsigned long STARTUP_GRACE_MS = 1500;
constexpr unsigned long POST_DETECT_SETTLE_MS = 180;
constexpr unsigned long DISPENSE_TIMEOUT_MS = 12000;
constexpr unsigned long STATUS_PRINT_MS = 1000;
constexpr bool STATUS_VERBOSE_LOG = false;
constexpr unsigned long PULSE_MIN_EDGE_GAP_MS = 20;
constexpr unsigned long PULSE_MIN_WIDTH_MS = 15;
constexpr unsigned long PULSE_MAX_WIDTH_MS = 350;
constexpr unsigned long COIN_IDLE_GAP_MS = 500;  // Increased from 250 for noise tolerance
constexpr unsigned long BILL_IDLE_GAP_MS = 900;  // Slow pulse mode needs longer gap

constexpr uint8_t M4_IN1_PIN = 18;
constexpr uint8_t M4_IN2_PIN = 19;
constexpr uint8_t M4_IN3_PIN = 23;
constexpr uint8_t M4_IN4_PIN = 27;  // moved from GPIO13 (unreliable); GPIO27 freed from IR4 (now on Uno A3)
constexpr uint8_t M4_IR_PIN = 35;   // local IR for motor 4 (input-only, free GPIO)

constexpr uint8_t BILL_PIN = 32;
constexpr uint8_t COIN_PIN = 14;
// IR5_1 moved to GPIO34 to avoid conflict with BILL_PIN on GPIO32.
constexpr uint8_t IR5_PINS[] = {34, 33, 25, 13, 26}; // IR5-4=GPIO13, IR5-5=GPIO26

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
constexpr uint8_t STORAGE_SLOT_COUNT = 5;
constexpr uint8_t ELEVATOR_LIFT_CHANNEL = 5; // CH5 elevator lift servo
constexpr uint8_t ELEVATOR_OUT_CHANNEL = 6;  // CH6 elevator output spinner
constexpr bool LIFT_DIRECTION_INVERTED = true;

// 360 servo commands: 90=stop, >90 rotate one direction, <90 rotate opposite direction.
constexpr uint8_t SERVO_STOP_CMD = 90;
constexpr uint8_t SPINNER_RUN_CMD = 180;
// Reverse direction to eject bills from storage during withdrawal (max speed).
constexpr uint8_t SPINNER_WITHDRAW_CMD = 0;
constexpr uint8_t STORAGE_FORWARD_CMD = SPINNER_RUN_CMD;
constexpr uint8_t STORAGE_BACKWARD_CMD = SPINNER_WITHDRAW_CMD;

uint8_t storageChannelMap[STORAGE_SLOT_COUNT] = {0, 1, 2, 3, 4};
bool storageDirectionInverted[STORAGE_SLOT_COUNT] = {true, false, false, false, false};
bool ir5ActiveLow[5] = {ACTIVE_LOW, ACTIVE_LOW, ACTIVE_LOW, ACTIVE_LOW, ACTIVE_LOW};
int8_t ir5PreferredSlot = 0;

uint8_t storageChannelForSlot(uint8_t slot) {
  if (slot >= STORAGE_SLOT_COUNT) {
    return SPINNER_CHANNEL_MIN;
  }
  return SPINNER_CHANNEL_MIN + storageChannelMap[slot];
}

uint8_t storageCommandForSlot(uint8_t slot, uint8_t command) {
  if (slot >= STORAGE_SLOT_COUNT) {
    return command;
  }
  if (!storageDirectionInverted[slot]) {
    return command;
  }
  if (command == STORAGE_FORWARD_CMD) {
    return STORAGE_BACKWARD_CMD;
  }
  if (command == STORAGE_BACKWARD_CMD) {
    return STORAGE_FORWARD_CMD;
  }
  return command;
}
constexpr uint8_t ELEVATOR_LIFT_UP_CMD = 110;    // Slowed from 122 for IR sensor detection time
constexpr uint8_t ELEVATOR_LIFT_HOLD_CMD = 92;
// Individual hold speeds for each slot (tuned for each floor position).
// Values >90 produce upward torque (with LIFT_DIRECTION_INVERTED=true) and are
// needed at every floor except the bottom to counteract gravity on the heavy
// carriage. Tune up/down in small steps if a floor still drifts.
constexpr uint8_t ELEVATOR_HOLD_IR5_1_CMD = 92;   // IR5-1 (top) - mild upward bias
constexpr uint8_t ELEVATOR_HOLD_IR5_2_CMD = 90;  // IR5-2 - upward bias against gravity (110 was too weak; raise if it still drifts down, lower if it creeps up)
constexpr uint8_t ELEVATOR_HOLD_IR5_3_CMD = 90;   // IR5-3 - servo stop, gearbox holds
constexpr uint8_t ELEVATOR_HOLD_IR5_4_CMD = 90;   // IR5-4 - servo stop, gearbox holds
constexpr uint8_t ELEVATOR_HOLD_IR5_5_CMD = 90;   // IR5-5 (bottom) - rests on frame, power cut
constexpr uint8_t ELEVATOR_LIFT_CATCH_SLOW_UP_CMD = 110;
constexpr uint8_t ELEVATOR_LIFT_RECOVER_CMD = 115;  // (legacy, no longer used by park) Park used to burst at this command; replaced by continuous creep.
// Park-recovery CREEP commands: very gentle, just outside the servo deadband
// so the motor turns slowly. We can stop the instant the target IR's raw
// reading triggers — no momentum overshoot.
constexpr uint8_t ELEVATOR_PARK_CREEP_UP_CMD = 95;     // logical 95  → physical 85 (very weak UP)
constexpr uint8_t ELEVATOR_PARK_CREEP_DOWN_CMD = 85;   // logical 85  → physical 95 (very weak DOWN)
constexpr uint8_t ELEVATOR_LIFT_DOWN_CMD = 70;   // Slowed from 60 for IR sensor detection time
// CH6 (output spinner) push direction. Hardware test confirmed `SERVO 6 180`
// is the FORWARD/push direction. Use 180 (full speed) to clear the servo's
// deadband — 170 was sometimes too close to neutral to start the motor.
constexpr uint8_t ELEVATOR_OUT_PUSH_CMD = 180;

constexpr unsigned long ELEVATOR_NUDGE_MS = 180;
constexpr unsigned long ELEVATOR_SETTLE_MS = 180;
constexpr unsigned long ELEVATOR_OUT_PULSE_MS = 8000;
constexpr unsigned long SPINNER_PULSE_MS = 300;
constexpr unsigned long ELEVATOR_RETURN_MS = 900;
constexpr unsigned long ELEVATOR_ROUTE_TIMEOUT_MS = 30000;
constexpr unsigned long BILL_ROUTE_SETTLE_MS = 5000; // wait after last bill detected before routing (5s)
constexpr unsigned long STORAGE_TEST_SPIN_MS = 1500;
constexpr unsigned long ELEVATOR_TO_IR5_1_TIMEOUT_MS = 25000;
constexpr unsigned long ELEVATOR_SLOT_MOVE_TIMEOUT_MS = 25000;
constexpr unsigned long ELEVATOR_SLOT_STOP_MS = 450;
constexpr unsigned long ELEVATOR_CATCH_SETTLE_MS = 120;
constexpr unsigned long ELEVATOR_CATCH_SLOW_UP_MS = 1300;
constexpr unsigned long ELEVATOR_PARK_RECOVER_BURST_MS = 180;
constexpr unsigned long ELEVATOR_PARK_RETRY_HOLD_MS = 150;
constexpr unsigned long ELEVATOR_LIFT_DIRECTION_TEST_MS = 2000;
// Withdrawal timing
constexpr unsigned long WITHDRAW_SPIN_MS = 1500;           // time to eject one bill (forward)
constexpr unsigned long WITHDRAW_REVERSE_MS = 500;         // brief reverse to settle next bill back into position
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
  // 20-peso coin produces 11-17 pulses (noise tolerance range)
  {11, 20.0f},
  {12, 20.0f},
  {13, 20.0f},
  {14, 20.0f},
  {15, 20.0f},
  {16, 20.0f},
  {17, 20.0f},
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

// Hardcoded fallback network. The customer can broadcast a phone hotspot
// with this SSID/password and the ATM will auto-connect on first boot.
// Once online they open the dashboard and use the SETWIFI form to switch
// the device onto their permanent WiFi (saved to NVS).
constexpr char WIFI_SSID[] = "CASHWIFI";
constexpr char WIFI_PASSWORD[] = "CASH12345!";
// Runtime-mutable copies. Loaded from NVS at boot (namespace "atm-wifi",
// keys "ssid"/"pass") with the constexpr fallback above as the default.
// Updated at runtime by the SETWIFI command from the dashboard.
String runtimeWifiSsid = WIFI_SSID;
String runtimeWifiPassword = WIFI_PASSWORD;
constexpr char DEPOSIT_API_URL[] = "https://cashmv.up.railway.app/api/deposit";
constexpr char COMMAND_API_URL[] = "https://cashmv.up.railway.app/api/command";
constexpr char DEVICE_STATUS_API_URL[] = "https://cashmv.up.railway.app/api/device/status";
constexpr unsigned long COMMAND_POLL_MS = 10000;
constexpr unsigned long COMMAND_POLL_RETRY_MS = 15000;
constexpr uint16_t COMMAND_HTTP_TIMEOUT_MS = 5000;
constexpr bool COMMAND_POLL_VERBOSE = false;
constexpr unsigned long LOOP_HEARTBEAT_MS = 3000;
constexpr bool LOOP_HEARTBEAT_VERBOSE = false;
constexpr unsigned long COMMAND_SKIP_LOG_MS = 5000;

struct MotorState {
  int sequenceIndex;
};

constexpr unsigned long M4_FORWARD_RUN_MS  = 14000;
constexpr unsigned long M4_BACKWARD_RUN_MS = 10000;
constexpr unsigned long M4_STEP_INTERVAL_US = 1500;

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
  WD_REVERSE_BILL,
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
PulseInput coinInput = {COIN_PIN, "coin", true, false, 0, 0, COIN_IDLE_GAP_MS, false, 0};
// Slow pulse mode can stretch high-time to ~300ms between falling edges.
// Use a larger idle gap so a single bill's pulse train is grouped correctly.
PulseInput billInput = {BILL_PIN, "bill", true, false, 0, 0, BILL_IDLE_GAP_MS, false, 0};
BillRouteJob billRoute = {};
WithdrawJob withdrawJob = {};

// When a WITHDRAW command includes BOTH bills and coins, we run the bill
// spinner job first (biggest denominations come out first) and stash the coin
// payout amount here. The coin steppers are only kicked off after the bill job
// finishes so the two motor systems never run at the same time.
struct PendingWithdraw {
  bool active;
  int coinAmount;
};
PendingWithdraw pendingWithdraw = {};
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
// Most recent non-target IR slot seen since park began. -1 means none yet.
// Used to infer recovery direction even after the drifted-to slot's stable
// detection has timed out (so we don't blindly default to UP and runaway).
int8_t elevatorParkLastDriftSlot = -1;
uint8_t pendingBillSlots[8] = {0};
uint8_t pendingBillCount = 0;
unsigned long billRouteReadyAfterMs = 0;

bool ir5RawLastState[5] = {false, false, false, false, false};
bool ir5StableState[5] = {false, false, false, false, false};
unsigned long ir5RawChangedMs[5] = {0, 0, 0, 0, 0};
unsigned long ir5DetectStartedMs[5] = {0, 0, 0, 0, 0};
bool unoOnline = false;
bool remoteTaskActive = false;
unsigned long remoteTaskSentMs = 0;
uint8_t remoteTaskMotor = 0;
uint8_t remoteTaskCount = 0;
uint8_t m4PinMap[4] = {0, 1, 2, 3}; // maps step columns → IN1/IN2/IN3/IN4 (try different orderings to find working wiring)
int m4IrBaselineLevel = HIGH;
bool motionArmed = MOTION_ARMED_DEFAULT;
bool pulseDebugEnabled = false;
InputDiag inputDiag = {false, 0, nullptr, true, 0, 0, 0, 0};
uint8_t currentLiftCommand = SERVO_STOP_CMD;
String usbCommandBuffer;
String unoLineBuffer;
unsigned long lastStatusPrintMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastCommandPollMs = 0;
unsigned long lastCommandPollErrorMs = 0;
uint8_t commandPollFailures = 0;
unsigned long lastLoopHeartbeatMs = 0;
unsigned long lastCommandSkipLogMs = 0;

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

// Add dwell timers for IR5 sensors
unsigned long ir5DwellStartMs[5] = {0, 0, 0, 0, 0};
bool ir5DwellActive[5] = {false, false, false, false, false};
constexpr unsigned long IR5_DWELL_MS = 150;

void stopElevatorPark();
void startElevatorPark(uint8_t slot);
void handleUsbCommand(String command);
void handleUnoLine(const String& line);

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

bool isIr5RawDetected(uint8_t slot) {
  if (slot > 4) {
    return false;
  }
  // Slot 3 (IR5-3) needs HIGH sensitivity: any hit out of 7 samples counts
  // as detected so the elevator never overshoots while passing through.
  if (slot == 2) {
    for (uint8_t i = 0; i < 7; ++i) {
      const int s = digitalRead(IR5_PINS[slot]);
      const bool det = ir5ActiveLow[slot] ? (s == LOW) : (s == HIGH);
      if (det) return true;
      delayMicroseconds(150);
    }
    return false;
  }
  // Slots 4 and 5 (IR5-4 / IR5-5) are electrically noisy: take a quick
  // majority vote across 7 samples spaced 150us apart (~1ms total). Wins
  // fast detection and rejects sub-millisecond chatter without long
  // debounce delay.
  if (slot == 3 || slot == 4) {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < 7; ++i) {
      const int s = digitalRead(IR5_PINS[slot]);
      const bool det = ir5ActiveLow[slot] ? (s == LOW) : (s == HIGH);
      if (det) ++hits;
      delayMicroseconds(150);
    }
    return hits >= 4; // majority of 7
  }
  const int raw = digitalRead(IR5_PINS[slot]);
  return ir5ActiveLow[slot] ? (raw == LOW) : (raw == HIGH);
}

int8_t currentIr5FaceSlot() {
  bool active[5] = {false, false, false, false, false};
  uint8_t activeCount = 0;
  int8_t firstActive = -1;

  for (uint8_t slot = 0; slot < 5; ++slot) {
    active[slot] = ir5StableState[slot];
    if (active[slot]) {
      if (firstActive < 0) {
        firstActive = static_cast<int8_t>(slot);
      }
      ++activeCount;
    }
  }

  if (activeCount == 0) {
    return -1;
  }
  if (activeCount == 1) {
    return firstActive;
  }

  // If multiple sensors are active, keep the previously valid slot if still active.
  if (ir5PreferredSlot >= 0 && ir5PreferredSlot < 5 && active[ir5PreferredSlot]) {
    return ir5PreferredSlot;
  }

  // Ambiguous (multiple active with no trusted prior slot): treat as no valid face detection.
  return -1;
}

bool isM4IrDetected() {
  const int raw = digitalRead(M4_IR_PIN);
  return raw != m4IrBaselineLevel;
}

void writeMotor4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  const uint8_t coils[4] = {a, b, c, d};
  const uint8_t pins[4] = {M4_IN1_PIN, M4_IN2_PIN, M4_IN3_PIN, M4_IN4_PIN};
  digitalWrite(pins[0], coils[m4PinMap[0]]);
  digitalWrite(pins[1], coils[m4PinMap[1]]);
  digitalWrite(pins[2], coils[m4PinMap[2]]);
  digitalWrite(pins[3], coils[m4PinMap[3]]);
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
  // Tolerance fallback: if pulse count is within +/- 1 of a unique map entry,
  // accept it. This catches noisy bill acceptor pulses where a single edge is
  // missed or doubled, instead of swallowing the bill silently.
  if (pulses == 0) return 0.0f;
  int matchIndex = -1;
  for (size_t index = 0; index < length; ++index) {
    const int diff = static_cast<int>(table[index].pulses) - static_cast<int>(pulses);
    if (diff >= -1 && diff <= 1) {
      if (matchIndex >= 0) {
        // Ambiguous (two adjacent map entries within tolerance); reject.
        return 0.0f;
      }
      matchIndex = static_cast<int>(index);
    }
  }
  if (matchIndex >= 0) {
    Serial.print(F("WARN pulse tolerance match pulses="));
    Serial.print(pulses);
    Serial.print(F(" assumed="));
    Serial.println(table[matchIndex].amount, 2);
    return table[matchIndex].amount;
  }
  return 0.0f;
}

// Try to connect to ssid/pass synchronously, blocking up to timeoutMs.
bool tryWifiConnect(const char* ssid, const char* pass, unsigned long timeoutMs) {
  if (ssid == nullptr || ssid[0] == '\0') return false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  delay(50);
  WiFi.begin(ssid, pass);
  Serial.print(F("Connecting to WiFi ssid="));
  Serial.println(ssid);
  const unsigned long startedMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedMs < timeoutMs) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected: "));
    Serial.println(WiFi.localIP());
    return true;
  }
  return false;
}

bool ensureWifi() {
  if (DEPOSIT_API_URL[0] == '\0') {
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

  // First try the active runtime credentials (NVS-saved or default fallback).
  if (runtimeWifiSsid.length() > 0) {
    if (tryWifiConnect(runtimeWifiSsid.c_str(), runtimeWifiPassword.c_str(), 5000)) {
      return true;
    }
  }

  // If the active creds fail and they aren't already the hardcoded fallback,
  // try the fallback (CASHWIFI). This is the customer-recovery path: power up
  // a phone hotspot named CASHWIFI/CASH12345! and the device will reconnect
  // even if a previously-saved SSID is no longer reachable.
  if (runtimeWifiSsid != WIFI_SSID) {
    Serial.print(F("WiFi primary failed; trying fallback ssid="));
    Serial.println(WIFI_SSID);
    if (tryWifiConnect(WIFI_SSID, WIFI_PASSWORD, 5000)) {
      return true;
    }
  }

  Serial.println(F("WiFi not connected"));
  return false;
}

// Load WiFi credentials from non-volatile storage (NVS). Returns true if a
// non-empty SSID was loaded; otherwise the compile-time defaults remain.
void loadWifiCredentials() {
  Preferences prefs;
  if (!prefs.begin("atm-wifi", true)) {
    Serial.println(F("WIFI prefs begin (read) failed; using defaults"));
    return;
  }
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  if (savedSsid.length() > 0) {
    runtimeWifiSsid = savedSsid;
    runtimeWifiPassword = savedPass;
    Serial.print(F("WIFI loaded from NVS ssid="));
    Serial.println(runtimeWifiSsid);
  } else {
    Serial.print(F("WIFI no NVS creds; default ssid="));
    Serial.println(runtimeWifiSsid);
  }
}

// Persist new WiFi credentials to NVS so they survive reboot.
bool saveWifiCredentials(const String& ssid, const String& pass) {
  Preferences prefs;
  if (!prefs.begin("atm-wifi", false)) {
    Serial.println(F("ERR wifi prefs begin (write) failed"));
    return false;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  return true;
}

// Apply new WiFi credentials at runtime: persist + disconnect + reconnect on
// next ensureWifi() tick. Used by the SETWIFI command from the dashboard.
void applyNewWifiCredentials(const String& ssid, const String& pass) {
  Serial.print(F("WIFI updating ssid="));
  Serial.println(ssid);
  if (!saveWifiCredentials(ssid, pass)) {
    return;
  }
  runtimeWifiSsid = ssid;
  runtimeWifiPassword = pass;
  WiFi.disconnect(false, true);
  lastWifiAttemptMs = 0;
  Serial.println(F("WIFI credentials saved; reconnect pending"));
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
  const String body = String("{\"amount\":") + String(amount, 2) +
                      String(",\"source\":\"") + String(source) +
                      String("\",\"pulses\":") + String(pulses) +
                      String("}");
  
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

void postWithdrawStatus(const char* state, int amount, bool active) {
  if (!ensureWifi()) {
    Serial.println(F("ERR: WiFi not connected"));
    return;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(secureClient, DEVICE_STATUS_API_URL)) {
    Serial.println(F("ERR: status http.begin() failed"));
    http.end();
    return;
  }

  http.addHeader("Content-Type", "application/json");
  const String body = String("{\"withdrawActive\":") + (active ? F("true") : F("false")) +
                      String(",\"withdrawState\":\"") + String(state) +
                      String("\",\"withdrawAmount\":") + String(amount) +
                      String("}");

  Serial.print(F("POST withdraw status state="));
  Serial.println(state);
  const int status = http.POST(body);
  Serial.print(F("HTTP status status="));
  Serial.println(status);
  http.end();
}

void pollRemoteCommands() {
  if (COMMAND_API_URL[0] == '\0') {
    return;
  }

  const unsigned long nowMs = millis();

  // Never block pulse-sensitive paths while acceptor pulses are in progress.
  if (coinInput.pulseActive || billInput.pulseActive || coinInput.pending || billInput.pending) {
    if (COMMAND_POLL_VERBOSE && (nowMs - lastCommandSkipLogMs >= COMMAND_SKIP_LOG_MS)) {
      lastCommandSkipLogMs = nowMs;
      Serial.print(F("CMD POLL skip=pulse-active coinActive="));
      Serial.print(coinInput.pulseActive ? F("1") : F("0"));
      Serial.print(F(" billActive="));
      Serial.print(billInput.pulseActive ? F("1") : F("0"));
      Serial.print(F(" coinPending="));
      Serial.print(coinInput.pending ? F("1") : F("0"));
      Serial.print(F(" billPending="));
      Serial.println(billInput.pending ? F("1") : F("0"));
    }
    return;
  }

  // Avoid extra network activity while local motion tasks are active.
  if (motor4Job.active || withdrawJob.active || billRoute.active) {
    if (COMMAND_POLL_VERBOSE && (nowMs - lastCommandSkipLogMs >= COMMAND_SKIP_LOG_MS)) {
      lastCommandSkipLogMs = nowMs;
      Serial.print(F("CMD POLL skip=motion-active m4="));
      Serial.print(motor4Job.active ? F("1") : F("0"));
      Serial.print(F(" withdraw="));
      Serial.print(withdrawJob.active ? F("1") : F("0"));
      Serial.print(F(" route="));
      Serial.println(billRoute.active ? F("1") : F("0"));
    }
    return;
  }

  if (!ensureWifi()) {
    if (COMMAND_POLL_VERBOSE && (nowMs - lastCommandSkipLogMs >= COMMAND_SKIP_LOG_MS)) {
      lastCommandSkipLogMs = nowMs;
      Serial.println(F("CMD POLL skip=wifi-disconnected"));
    }
    return;
  }

  const unsigned long pollDelayMs = commandPollFailures > 0 ? COMMAND_POLL_RETRY_MS : COMMAND_POLL_MS;
  if (nowMs - lastCommandPollMs < pollDelayMs) {
    if (COMMAND_POLL_VERBOSE && (nowMs - lastCommandSkipLogMs >= COMMAND_SKIP_LOG_MS)) {
      lastCommandSkipLogMs = nowMs;
      Serial.print(F("CMD POLL wait remainingMs="));
      Serial.println(pollDelayMs - (nowMs - lastCommandPollMs));
    }
    return;
  }
  lastCommandPollMs = nowMs;

  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // skip cert verification

  HTTPClient http;
  http.setConnectTimeout(COMMAND_HTTP_TIMEOUT_MS);
  http.setTimeout(COMMAND_HTTP_TIMEOUT_MS);

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
    Serial.print(F("WARN: command HTTP poll failed count="));
    Serial.print(commandPollFailures + 1);
    Serial.print(F(" err="));
    if (status < 0) {
      Serial.println(http.errorToString(status));
    } else {
      Serial.println(status);
    }
    commandPollFailures = static_cast<uint8_t>(min<int>(commandPollFailures + 1, 10));
    lastCommandPollErrorMs = nowMs;
    http.end();
    return;
  }

  if (commandPollFailures > 0) {
    commandPollFailures = 0;
  }

  const String payload = http.getString();
  http.end();

  if (COMMAND_POLL_VERBOSE) {
    Serial.print(F("CMD POLL payload="));
    Serial.println(payload);
  }

  bool handledCommand = false;
  const int keyIndex = payload.indexOf("\"commands\"");
  if (keyIndex >= 0) {
    const int listStart = payload.indexOf('[', keyIndex);
    const int listEnd = payload.indexOf(']', listStart);
    if (listStart >= 0 && listEnd >= 0 && listEnd > listStart) {
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
          handledCommand = true;
        }

        scan = close + 1;
      }
    }
  }

  // Compatibility fallback in case backend returns a single command string.
  if (!handledCommand) {
    const int singleKey = payload.indexOf("\"command\"");
    if (singleKey >= 0) {
      const int colon = payload.indexOf(':', singleKey);
      const int firstQuote = payload.indexOf('"', colon + 1);
      const int secondQuote = payload.indexOf('"', firstQuote + 1);
      if (colon >= 0 && firstQuote >= 0 && secondQuote > firstQuote) {
        String cmd = payload.substring(firstQuote + 1, secondQuote);
        cmd.trim();
        if (cmd.length() > 0) {
          Serial.print(F("REMOTE CMD: "));
          Serial.println(cmd);
          handleUsbCommand(cmd);
          handledCommand = true;
        }
      }
    }
  }

  if (COMMAND_POLL_VERBOSE && !handledCommand) {
    Serial.println(F("CMD POLL no command"));
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
  const int8_t faceSlot = currentIr5FaceSlot();
  if (faceSlot >= 0) {
    ir5PreferredSlot = faceSlot;
  }
  return faceSlot == static_cast<int8_t>(slot);
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

  // SAFETY: if the carriage has reached the physical bottom (IR5-5) and the
  // park system is trying to hold it at any other floor, it means the carriage
  // fell and the hold is too weak (or the polarity is wrong). In either case
  // continuing to drive the motor will only wind the string the wrong way
  // around the spool. Cut all lift power immediately.
  if (elevatorParkActive && elevatorParkSlot != 4 && isIr5Detected(4)) {
    if (currentLiftCommand != SERVO_STOP_CMD) {
      currentLiftCommand = SERVO_STOP_CMD;
      pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
      Serial.print(F("LIFT SAFETY: carriage hit bottom while parking at IR5-"));
      Serial.print(elevatorParkSlot + 1);
      Serial.println(F(" - power cut"));
    }
    stopElevatorPark();
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

  const bool liftMoving = (currentLiftCommand != SERVO_STOP_CMD);
  unsigned long holdMs = liftMoving ? IR5_DETECT_HOLD_MOVING_MS : IR5_DETECT_HOLD_IDLE_MS;
  if (slot == 3) {
    holdMs += IR5_SLOT4_EXTRA_HOLD_MS;
  }

  if (isIr5Detected(slot)) {
    if (ir5DetectStartedMs[slot] == 0) {
      ir5DetectStartedMs[slot] = nowMs;
      return false;
    }
    return (nowMs - ir5DetectStartedMs[slot]) >= holdMs;
  }

  ir5DetectStartedMs[slot] = 0;
  return false;
}

// Forward declaration: moveLiftDirectToSlot is defined later in this file but
// is invoked from BillRoute helpers that appear earlier.
bool moveLiftDirectToSlot(uint8_t slot);

void startBillRouteIfPending() {
  // For bill input flow, always finish catch-to-IR5-1 first before routing.
  if (billRoute.active || pendingBillCount == 0 || liftToIr51Active || withdrawJob.active) {
    return;
  }
  // Wait for bill settle delay before routing.
  if (billRouteReadyAfterMs > 0 && millis() < billRouteReadyAfterMs) {
    return;
  }
  if (!motionArmed || !SERVOS_ENABLED) {
    return;
  }

  const uint8_t targetSlot = pendingBillSlots[0];

  Serial.print(F("BILL ROUTE START slot="));
  Serial.println(targetSlot + 1);

  // Use the proven IR5 navigation: raw-IR arrival + park hold (cmd=90) + creep
  // recovery. This is the same path used by the manual IR5-N command and
  // replaces the old bang-bang lift logic that drifted at non-top floors.
  stopElevatorPark();
  if (!moveLiftDirectToSlot(targetSlot)) {
    Serial.print(F("BILL ROUTE NAV FAIL slot="));
    Serial.println(targetSlot + 1);
    setLiftCommand(SERVO_STOP_CMD);
    popBillSlot();
    return;
  }

  // Carriage is now parked at target IR5 with hold cmd. Begin transfer-out:
  // CH6 pushes bill into storage, destination slot motor assists feed.
  billRoute.active = true;
  billRoute.stage = ROUTE_TRANSFER_OUT;
  billRoute.targetSlot = targetSlot;
  billRoute.startedMs = millis();
  billRoute.stageStartedMs = billRoute.startedMs;
  billRoute.liftRunning = false;

  // Only CH6 (output spinner on the elevator) runs during a deposit. CH0..CH4
  // are dispensers used for WITHDRAWAL only — they must stay idle here.
  setServoAngle(ELEVATOR_OUT_CHANNEL, ELEVATOR_OUT_PUSH_CMD);
  Serial.print(F("BILL ROUTE TRANSFER slot="));
  Serial.print(targetSlot + 1);
  Serial.print(F(" ch6="));
  Serial.println(ELEVATOR_OUT_PUSH_CMD);
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
  pcaSetPwm(storageChannelForSlot(slot), 0, 0);
}

void spinStorageMotor(uint8_t slot, uint8_t command) {
  if (!SERVOS_ENABLED) {
    Serial.println(F("ERR servos-disabled"));
    return;
  }
  if (!motionArmed) {
    Serial.println(F("ERR motion-disarmed"));
    return;
  }
  if (slot > 4) {
    Serial.println(F("ERR storage slot 1..5"));
    return;
  }

  const uint8_t mappedCommand = storageCommandForSlot(slot, command);
  setServoAngle(storageChannelForSlot(slot), mappedCommand);
  Serial.print(F("OK STORAGE slot="));
  Serial.print(slot + 1);
  Serial.print(F(" cmd="));
  Serial.print(mappedCommand);
  Serial.print(F(" dir="));
  if (mappedCommand == SPINNER_RUN_CMD) {
    Serial.println(F("FORWARD"));
  } else if (mappedCommand == SPINNER_WITHDRAW_CMD) {
    Serial.println(F("BACKWARD"));
  } else {
    Serial.println(F("CUSTOM"));
  }
}

void spinAllStorageMotors(uint8_t command) {
  if (!SERVOS_ENABLED) {
    Serial.println(F("ERR servos-disabled"));
    return;
  }
  if (!motionArmed) {
    Serial.println(F("ERR motion-disarmed"));
    return;
  }

  for (uint8_t slot = 0; slot < 5; ++slot) {
    setServoAngle(storageChannelForSlot(slot), storageCommandForSlot(slot, command));
  }

  Serial.print(F("OK STORAGE all cmd="));
  Serial.print(command);
  Serial.print(F(" dir="));
  if (command == SPINNER_RUN_CMD) {
    Serial.println(F("FORWARD"));
  } else if (command == SPINNER_WITHDRAW_CMD) {
    Serial.println(F("BACKWARD"));
  } else {
    Serial.println(F("CUSTOM"));
  }
}

void runStorageTest(uint8_t slot) {
  if (!SERVOS_ENABLED) {
    Serial.println(F("ERR servos-disabled"));
    return;
  }
  if (slot > 4) {
    Serial.println(F("ERR storage slot 1..5"));
    return;
  }
  if (!motionArmed) {
    motionArmed = true;
    pcaAllChannelsOff();
    Serial.println(F("OK ARMED (STORAGETEST)"));
  }

  Serial.print(F("STORAGETEST slot="));
  Serial.print(slot + 1);
  Serial.println(F(" FORWARD"));
  setServoAngle(storageChannelForSlot(slot), storageCommandForSlot(slot, SPINNER_RUN_CMD));
  delay(STORAGE_TEST_SPIN_MS);

  Serial.print(F("STORAGETEST slot="));
  Serial.print(slot + 1);
  Serial.println(F(" BACKWARD"));
  setServoAngle(storageChannelForSlot(slot), storageCommandForSlot(slot, SPINNER_WITHDRAW_CMD));
  delay(STORAGE_TEST_SPIN_MS);

  stopStorageMotor(slot);
  Serial.print(F("STORAGETEST slot="));
  Serial.print(slot + 1);
  Serial.println(F(" STOP"));
}

void printStorageConfig() {
  Serial.println(F("STORAGE CONFIG:"));
  for (uint8_t slot = 0; slot < STORAGE_SLOT_COUNT; ++slot) {
    Serial.print(F("  slot="));
    Serial.print(slot + 1);
    Serial.print(F(" channel="));
    Serial.print(storageChannelMap[slot]);
    Serial.print(F(" inverted="));
    Serial.println(storageDirectionInverted[slot] ? F("1") : F("0"));
  }
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
    case ROUTE_FIND_LEVEL:
    case ROUTE_NUDGE_ABOVE:
    case ROUTE_SPINNER_PULSE:
    case ROUTE_RETURN_HOME: {
      // These legacy stages are no longer used: startBillRouteIfPending now
      // performs the to-target nav synchronously via moveLiftDirectToSlot,
      // and the return-home leg is handled below at the end of TRANSFER_OUT.
      // If we ever land here, treat it as a no-op skip to TRANSFER_OUT.
      billRoute.stage = ROUTE_TRANSFER_OUT;
      billRoute.stageStartedMs = nowMs;
      break;
    }

    case ROUTE_TRANSFER_OUT: {
      if (nowMs - billRoute.stageStartedMs >= ELEVATOR_OUT_PULSE_MS) {
        stopElevatorOutMotor();
        stopStorageMotor(billRoute.targetSlot);

        // Return home using the proven IR5 navigation (raw arrival + park
        // hold cmd=90 + creep recovery), same path the manual IR5-1 uses.
        Serial.println(F("BILL ROUTE RETURN_HOME"));
        stopElevatorPark();
        if (moveLiftDirectToSlot(0)) {
          finishBillRoute(F("DONE"));
        } else {
          finishBillRoute(F("RETURN_FAIL"));
        }
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
  m4IrBaselineLevel = digitalRead(M4_IR_PIN);
  motor4Job.sensorArmed = !isM4IrDetected();
  motor4Job.goingForward = true;
  motor4Job.startedMs = millis();
  motor4Job.coinDeadlineMs = motor4Job.startedMs + JOB_MAX_RUNTIME_MS;
  motor4Job.phaseStartedMs = motor4Job.startedMs;
  motor4Job.detectStartedMs = 0;
  motor4Job.lastStepUs = micros();
  Serial.print(F("OK DISPENSE motor=4 count="));
  Serial.println(count);
  Serial.println(F("M4 PHASE=FWD"));
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

  UnoSerial.print('@');
  UnoSerial.print(F("DISPENSE "));
  UnoSerial.print(nextTask.motor);
  UnoSerial.print(' ');
  UnoSerial.println(nextTask.count);
  Serial.print(F(">UNO>@DISPENSE "));
  Serial.print(nextTask.motor);
  Serial.print(' ');
  Serial.println(nextTask.count);
  remoteTaskActive = true;
  remoteTaskSentMs = millis();
  remoteTaskMotor = nextTask.motor;
  remoteTaskCount = nextTask.count;
  Serial.print(F("SENT @DISPENSE motor="));
  Serial.print(nextTask.motor);
  Serial.print(F(" count="));
  Serial.print(nextTask.count);
  Serial.print(F(" unoOnline="));
  Serial.println(unoOnline ? F("1") : F("0"));
}

void stopAllDispense() {
  taskCount = 0;
  motor4Job = {};
  remoteTaskActive = false;
  releaseMotor4();
  UnoSerial.print('@');
  UnoSerial.println(F("STOP"));
  Serial.println(F(">UNO>@STOP"));
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
  Serial.println(F("  IR5POL <slot1to5> <LOW|HIGH|0|1>"));
  Serial.println(F("  M4IR (print raw + baseline + detected)"));
  Serial.println(F("  M4COIL a b c d   (raw 0/1 to IN1..IN4)"));
  Serial.println(F("  M4PINMAP p0 p1 p2 p3 (remap step cols 0-3 to IN1..IN4)"));
  Serial.println(F("  STORAGE <slot1to5|ALL> <FORWARD|BACKWARD|STOP|cmd0to180>"));
  Serial.println(F("  STORAGESTOP <slot1to5|ALL>"));
  Serial.println(F("  STORAGETEST <slot1to5>"));
  Serial.println(F("  STORAGECONF"));
  Serial.println(F("  STORAGEMAP <slot1to5> <channel0to4>"));
  Serial.println(F("  STORAGEINV <slot1to5> <0|1>"));
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
  switch (slot) {
    case 0:
      return ELEVATOR_HOLD_IR5_1_CMD;  // IR5-1 (top floor)
    case 1:
      return ELEVATOR_HOLD_IR5_2_CMD;  // IR5-2 (floor 2)
    case 2:
      return ELEVATOR_HOLD_IR5_3_CMD;  // IR5-3 (floor 3)
    case 3:
      return ELEVATOR_HOLD_IR5_4_CMD;  // IR5-4 (floor 4)
    case 4:
      return ELEVATOR_HOLD_IR5_5_CMD;  // IR5-5 (bottom floor)
    default:
      return SERVO_STOP_CMD;
  }
}

int8_t currentIr5Slot() {
  for (uint8_t slot = 0; slot < 5; ++slot) {
    if (isIr5Detected(slot)) {
      return static_cast<int8_t>(slot);
    }
  }
  return -1;
}

bool validateLiftDirection(uint8_t testCommand, uint8_t targetSlot) {
  if (testCommand == SERVO_STOP_CMD) {
    return true;  // Stop commands don't need validation
  }

  const int8_t positionBefore = currentIr5Slot();
  
  // Don't test if already at hard limits (IR5-1 at slot 0 or IR5-5 at slot 4)
  // These limits are enforced by clampLiftCommandToLimits anyway
  if (positionBefore == 0 || positionBefore == 4) {
    return true;  // Already at a limit, hardware limits will prevent invalid moves
  }

  // If we're between sensors there is no reliable way to pre-measure direction
  // (the 3s test would compare -1 vs -1). Trust the caller and let
  // moveLiftToIr5Slot's own timeout / closed-loop correction catch real faults.
  if (positionBefore < 0) {
    return true;
  }

  // Test if within 1 slot of target (IR5-3 high sensitivity won't cause false failures)
  if (positionBefore >= 0 && targetSlot >= 0 && targetSlot <= 4) {
    int8_t distance = positionBefore > static_cast<int8_t>(targetSlot) 
                      ? (positionBefore - static_cast<int8_t>(targetSlot))
                      : (static_cast<int8_t>(targetSlot) - positionBefore);
    if (distance <= 1) {
      return true;  // Already very close to target, safe to move
    }
  }
  
  // Direction test for general movement between distant slots (extended for slower speed)
  setLiftCommand(testCommand);
  delay(3000);  // Increased from ELEVATOR_LIFT_DIRECTION_TEST_MS for slower speeds
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

  if (positionBefore < 0 && positionAfter >= 0) {
    return true;  // Detected a slot when previously undetected
  }

  Serial.print(F("WARN lift direction test: no movement ("));
  Serial.print(positionBefore);
  Serial.print(F("→"));
  Serial.print(positionAfter);
  Serial.println(F(")"));
  return false;  // Direction blocked or no movement detected
}

bool moveLiftToIr5Slot(uint8_t slot, uint8_t moveCommand, unsigned long timeoutMs) {
  // Send initial movement command immediately so the lift starts moving even if
  // the carriage is currently between IR sensors.
  setLiftCommand(moveCommand);
  const unsigned long startMs = millis();
  uint8_t activeCommand = moveCommand;

  Serial.print(F("MOVE LOOP target=IR5-"));
  Serial.print(slot + 1);
  Serial.print(F(" initialCmd="));
  Serial.println(moveCommand);

  while (millis() - startMs < timeoutMs) {
    // ARRIVAL: raw read for fast catch (carriage falls past beam in <150ms,
    // so the dwell-debounced ir5StableState would miss it entirely).
    if (isIr5RawDetected(slot)) {
      setLiftCommand(getElevatorHoldCommand(slot));
      Serial.print(F("MOVE LOOP arrived IR5-"));
      Serial.print(slot + 1);
      Serial.print(F(" elapsedMs="));
      Serial.println(millis() - startMs);
      return true;
    }

    // OVERSHOOT: use dwell-debounced stable state. The raw IR5-3 read uses a
    // hyper-sensitive any-of-7-samples pattern that fires on noise even when
    // the carriage is far away, which would cause spurious direction flips
    // (oscillation). The stable state filters those out.
    int8_t overshootSlot = -1;
    for (uint8_t i = 0; i < 5; ++i) {
      if (i != slot && ir5StableState[i]) {
        overshootSlot = static_cast<int8_t>(i);
        break;
      }
    }
    if (overshootSlot >= 0) {
      // IR5-1 = slot 0 (top), IR5-5 = slot 4 (bottom). Slot index INCREASES
      // going down, so a hit at slot > target means we are BELOW target.
      uint8_t desiredCommand = (overshootSlot > static_cast<int8_t>(slot))
                                   ? ELEVATOR_LIFT_UP_CMD
                                   : ELEVATOR_LIFT_DOWN_CMD;
      if (desiredCommand != activeCommand) {
        setLiftCommand(desiredCommand);
        activeCommand = desiredCommand;
        Serial.print(F("MOVE LOOP correction at IR5-"));
        Serial.print(overshootSlot + 1);
        Serial.print(F(" newCmd="));
        Serial.println(desiredCommand);
      }
    }
    delay(5);
  }
  setLiftCommand(SERVO_STOP_CMD);
  Serial.print(F("MOVE LOOP timeout target=IR5-"));
  Serial.println(slot + 1);
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
  if (!validateLiftDirection(moveCommand, slot)) {
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

  // No post-arrival nudge: the raw IR detection in moveLiftToIr5Slot already
  // catches the carriage the instant the beam breaks, and an extra upward
  // nudge would push the carriage past the target before the park hold takes
  // over (causing the recovery loop to keep climbing toward IR5-1).

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
  Serial.print(digitalRead(M4_IR_PIN));
  Serial.print(F(" ir4base="));
  Serial.print(m4IrBaselineLevel);
  Serial.print(F(" ir4det="));
  Serial.print(isM4IrDetected() ? F("1") : F("0"));
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
  elevatorParkLastDriftSlot = -1;
  // Clear stale stable-state on OTHER slots so a previous sequence's stable
  // detection (e.g. boot LIFTTO1 leaving IR5-1 stable) cannot trigger a
  // bogus recovery burst the instant we start parking at this slot.
  for (uint8_t i = 0; i < 5; ++i) {
    if (i != slot) {
      ir5StableState[i] = false;
      ir5DetectStartedMs[i] = 0;
    }
  }
  if (slot == 4) {
    // IR5-5 is the physical bottom: rests on the frame, no power needed.
    currentLiftCommand = SERVO_STOP_CMD;
    pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
  } else {
    // All other floors: actively hold against gravity.
    setLiftCommand(getElevatorHoldCommand(slot));
  }
}

void serviceElevatorPark() {
  if (!elevatorParkActive || !motionArmed || billRoute.active || liftToIr51Active) {
    return;
  }

  const unsigned long nowMs = millis();

  // IR5-5 (bottom): the carriage rests on the mechanical bottom. Keep the
  // servo channel fully de-energised so it cannot fight the rest position.
  if (elevatorParkSlot == 4) {
    elevatorParkRecovering = false;
    elevatorParkRecoverStartedMs = 0;
    elevatorParkRetryAfterMs = 0;
    if (currentLiftCommand != SERVO_STOP_CMD) {
      currentLiftCommand = SERVO_STOP_CMD;
      pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
    }
    return;
  }

  const uint8_t holdCmd = getElevatorHoldCommand(elevatorParkSlot);

  if (isIr5StableDetected(elevatorParkSlot, nowMs)) {
    // IR sensor confirms we are at the parked floor. Continuously reassert the
    // per-slot hold value so the carriage cannot drift even if some other
    // service routine briefly altered the lift command.
    if (elevatorParkRecovering) {
      elevatorParkRecovering = false;
      elevatorParkRecoverStartedMs = 0;
      elevatorParkRetryAfterMs = 0;
      Serial.print(F("ELEVATOR PARK RESTORED slot="));
      Serial.println(elevatorParkSlot + 1);
    }
    // Back on target: forget previous drift direction so the next loss of
    // beam picks direction fresh from current evidence.
    elevatorParkLastDriftSlot = -1;
    if (currentLiftCommand != holdCmd) {
      setLiftCommand(holdCmd);
    }
    return;
  }
  // Update the remembered drift slot from any currently stable non-target IR
  // BEFORE deciding what to do. This lets us infer direction later even when
  // the carriage has overshot back into the gap between sensors.
  for (uint8_t i = 0; i < 5; ++i) {
    if (i != elevatorParkSlot && ir5StableState[i]) {
      elevatorParkLastDriftSlot = static_cast<int8_t>(i);
      break;
    }
  }

  // Lost the IR sensor: creep gently back toward the target. We do NOT use
  // momentum-style bursts because the heavy carriage flies past the beam in
  // less than the dwell debounce window, causing the loop to keep climbing
  // until it hits a hard limit. Instead, set a weak continuous command and
  // re-evaluate every loop tick. The move stops the moment isIr5StableDetected
  // succeeds at the top of this function on a future tick (which uses the raw
  // reading via the existing dwell-detection layer).
  bool recoverUpward = true;
  int8_t evidenceSlot = -1;
  for (uint8_t i = 0; i < 5; ++i) {
    if (i != elevatorParkSlot && ir5StableState[i]) {
      evidenceSlot = static_cast<int8_t>(i);
      break;
    }
  }
  if (evidenceSlot < 0) {
    evidenceSlot = elevatorParkLastDriftSlot;
  }
  if (evidenceSlot >= 0) {
    recoverUpward = (evidenceSlot > static_cast<int8_t>(elevatorParkSlot));
  }

  // Abort the creep the instant the target's raw IR triggers — don't wait
  // for dwell debounce, the carriage can cross the beam in <150ms.
  if (isIr5RawDetected(elevatorParkSlot)) {
    elevatorParkRecovering = false;
    if (currentLiftCommand != holdCmd) {
      setLiftCommand(holdCmd);
    }
    return;
  }

  const uint8_t creepCmd = recoverUpward
      ? ELEVATOR_PARK_CREEP_UP_CMD
      : ELEVATOR_PARK_CREEP_DOWN_CMD;
  // Mark park as recovering so updateElevatorParkHold doesn't fight us by
  // reasserting the hold command on every tick.
  const bool wasRecovering = elevatorParkRecovering;
  elevatorParkRecovering = true;
  if (currentLiftCommand != creepCmd) {
    setLiftCommand(creepCmd);
  }
  // Only print on direction change (or first entry into creep) so the serial
  // log isn't flooded.
  static bool lastCreepUp = false;
  if (!wasRecovering || lastCreepUp != recoverUpward) {
    lastCreepUp = recoverUpward;
    Serial.print(F("ELEVATOR PARK CREEP slot="));
    Serial.print(elevatorParkSlot + 1);
    Serial.println(recoverUpward ? F(" dir=UP") : F(" dir=DOWN"));
  }
}

void updateElevatorParkHold() {
  if (!elevatorParkActive || elevatorParkRecovering) {
    return;
  }
  if (elevatorParkSlot == 4) {
    if (currentLiftCommand != SERVO_STOP_CMD) {
      currentLiftCommand = SERVO_STOP_CMD;
      pcaSetPwm(ELEVATOR_LIFT_CHANNEL, 0, 0);
    }
    return;
  }
  const uint8_t holdCmd = getElevatorHoldCommand(elevatorParkSlot);
  if (holdCmd != currentLiftCommand) {
    setLiftCommand(holdCmd);
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
  const unsigned long nowMs = millis();
  const bool liftMoving = (currentLiftCommand != SERVO_STOP_CMD);
  for (uint8_t index = 0; index < 5; ++index) {
    unsigned long debounceMs = liftMoving ? IR5_EDGE_DEBOUNCE_MOVING_MS : IR5_EDGE_DEBOUNCE_IDLE_MS;
    if (index == 2) {
      debounceMs = liftMoving ? 2 : 25;
    }
    if (index == 3 || index == 4) {
      debounceMs = liftMoving ? 30 : 120;
    }
    const bool rawDetected = isIr5RawDetected(index);

    // --- Dwell logic: require continuous detection for IR5_DWELL_MS ---
    if (rawDetected) {
      if (!ir5DwellActive[index]) {
        ir5DwellActive[index] = true;
        ir5DwellStartMs[index] = nowMs;
      }
    } else {
      ir5DwellActive[index] = false;
      ir5DwellStartMs[index] = 0;
    }
    bool dwellDetected = ir5DwellActive[index] && (nowMs - ir5DwellStartMs[index] >= IR5_DWELL_MS);

    if (dwellDetected != ir5RawLastState[index]) {
      if (IR5_RAW_EVENT_LOG) {
        Serial.print(F("IR5-"));
        Serial.print(index + 1);
        Serial.print(F(" RAW DWELL "));
        Serial.println(dwellDetected ? F("DETECTED") : F("CLEARED"));
      }
      ir5RawLastState[index] = dwellDetected;
      ir5RawChangedMs[index] = nowMs;
    }

    if (dwellDetected != ir5StableState[index] &&
      (nowMs - ir5RawChangedMs[index]) >= debounceMs) {
      ir5StableState[index] = dwellDetected;
      if (liftMoving) {
        Serial.print(F("IR5-"));
        Serial.print(index + 1);
        Serial.print(F(" STABLE DWELL "));
        Serial.println(dwellDetected ? F("DETECTED") : F("CLEARED"));
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
  if (nowUs - motor4Job.lastStepUs >= M4_STEP_INTERVAL_US) {
    if (motor4Job.goingForward) {
      stepMotor4Forward();
    } else {
      stepMotor4Backward();
    }
    motor4Job.lastStepUs = nowUs;
  }

  const bool detected = isM4IrDetected();
  if (!motor4Job.sensorArmed) {
    if (!detected) {
      motor4Job.sensorArmed = true;
      motor4Job.detectStartedMs = 0;
    }
    return;
  }

  if (detected) {
    // Count immediately on first beam-break edge so fast-falling coins are not missed.
    ++motor4Job.dispensedCount;
    motor4Job.sensorArmed = false;
    motor4Job.detectStartedMs = 0;
    Serial.print(F("EVENT motor=4 dispensed="));
    Serial.println(motor4Job.dispensedCount);

    if (motor4Job.dispensedCount >= motor4Job.requestedCount) {
      finishLocalMotor4();
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

// Helper: parse "key=value" from command string
int sscanfValue(const String& cmd, const char* key) {
  int idx = cmd.indexOf(key);
  if (idx < 0) return 0;
  int valueStart = idx + strlen(key);
  int valueEnd = valueStart;
  while (valueEnd < cmd.length() && cmd.charAt(valueEnd) >= '0' && cmd.charAt(valueEnd) <= '9') {
    ++valueEnd;
  }
  if (valueEnd <= valueStart) return 0;
  return cmd.substring(valueStart, valueEnd).toInt();
}

// Withdrawal without recalculation - uses provided counts directly
bool startWithdrawJobWithCounts(uint8_t counts[5]) {
  if (withdrawJob.active) { Serial.println(F("ERR withdraw-busy")); return false; }
  if (!motionArmed)        { Serial.println(F("ERR withdraw-disarmed")); return false; }
  if (!SERVOS_ENABLED)     { Serial.println(F("ERR withdraw-servos-disabled")); return false; }
  if (billRoute.active)    { Serial.println(F("ERR withdraw-routing-busy")); return false; }

  if (elevatorParkActive) stopElevatorPark();
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

  int totalAmount = counts[0] * 20 + counts[1] * 50 + counts[2] * 100 + counts[3] * 500 + counts[4] * 1000;
  
  Serial.print(F("WITHDRAW START bills amount="));
  Serial.print(totalAmount);
  Serial.print(F(" 20x")); Serial.print(counts[0]);
  Serial.print(F(" 50x")); Serial.print(counts[1]);
  Serial.print(F(" 100x")); Serial.print(counts[2]);
  Serial.print(F(" 500x")); Serial.print(counts[3]);
  Serial.print(F(" 1000x")); Serial.println(counts[4]);
  postWithdrawStatus("dispensing", totalAmount, true);

  if (withdrawJob.currentSlot >= 5) {
    // No bills to dispense
    withdrawJob.active = false;
    Serial.println(F("WITHDRAW no bills to dispense"));
    postWithdrawStatus("complete", 0, false);
    return true;
  }

  withdrawJob.billsLeft = withdrawJob.slotCounts[withdrawJob.currentSlot];
  setServoAngle(
    storageChannelForSlot(withdrawJob.currentSlot),
    storageCommandForSlot(withdrawJob.currentSlot, STORAGE_FORWARD_CMD)
  );
  withdrawJob.stage = WD_SPIN_BILL;
  // Use current time, NOT withdrawJob.startedMs: postWithdrawStatus() above
  // can take several seconds for the HTTP call, which would make the
  // 2-second spin window already-elapsed on the very first service tick.
  withdrawJob.stageStartedMs = millis();
  Serial.print(F("WITHDRAW SPIN slot="));
  Serial.print(withdrawJob.currentSlot + 1);
  Serial.print(F(" bills_left=")); Serial.println(withdrawJob.billsLeft);
  return true;
}

// ---------------------------------------------------------------------------
// Withdrawal state machine – dispenses bills from storage using the configured slot direction.
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

// Kick off the deferred coin payout once the bill spinner job has finished
// and no other motor activity is happening. Enforces strict one-motor-at-a-
// time dispensing (bills first, then coins).
void servicePendingWithdraw() {
  if (!pendingWithdraw.active) return;
  if (withdrawJob.active) return;
  if (taskCount > 0) return;
  if (remoteTaskActive) return;
  if (motor4Job.active) return;
  if (billRoute.active) return;

  const int amount = pendingWithdraw.coinAmount;
  pendingWithdraw = {};
  Serial.print(F("WITHDRAW coins RESUMING (bills done) amount="));
  Serial.println(amount);
  handlePayoutCommand(amount);
  Serial.print(F("WITHDRAW coins QUEUED tasks_after="));
  Serial.print(taskCount);
  Serial.print(F(" remoteActive="));
  Serial.print(remoteTaskActive ? F("1") : F("0"));
  Serial.print(F(" motor4Active="));
  Serial.println(motor4Job.active ? F("1") : F("0"));
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
    postWithdrawStatus("complete", 0, false);
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

  const bool hasBills = (counts[0] || counts[1] || counts[2] || counts[3] || counts[4]);
  if (coinRemainder > 0 && hasBills) {
    // Defer coin payout until the bill spinner job finishes so only one motor
    // system runs at a time, and the larger denominations come out first.
    pendingWithdraw.active = true;
    pendingWithdraw.coinAmount = coinRemainder;
    Serial.print(F("WITHDRAW coins DEFERRED until bills finish amount="));
    Serial.println(coinRemainder);
  } else if (coinRemainder > 0) {
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
  postWithdrawStatus("dispensing", amount, true);

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
        // Reverse briefly so the next bill in the stack settles back into position.
        setServoAngle(
          storageChannelForSlot(withdrawJob.currentSlot),
          storageCommandForSlot(withdrawJob.currentSlot, STORAGE_BACKWARD_CMD)
        );
        withdrawJob.stage = WD_REVERSE_BILL;
        withdrawJob.stageStartedMs = nowMs;
        Serial.print(F("WITHDRAW REVERSE slot="));
        Serial.println(withdrawJob.currentSlot + 1);
      }
      break;
    }
    case WD_REVERSE_BILL: {
      if (nowMs - withdrawJob.stageStartedMs >= WITHDRAW_REVERSE_MS) {
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
        setServoAngle(
          storageChannelForSlot(withdrawJob.currentSlot),
          storageCommandForSlot(withdrawJob.currentSlot, STORAGE_FORWARD_CMD)
        );
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
      postWithdrawStatus("complete", 0, false);
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
  // PC-bridge inbound: lines like "<UNO<PONG" are treated as if they came from UnoSerial.
  if (command.startsWith("<UNO<")) {
    handleUnoLine(command.substring(5));
    return;
  }
  String commandUpper = command;
  commandUpper.toUpperCase();

  if (commandUpper == F("STATUS")) {
    printStatus();
    UnoSerial.print('@');
    UnoSerial.println(F("STATUS"));
    Serial.println(F(">UNO>@STATUS"));
    return;
  }

  // SETWIFI ssid="..." pass="..."  -- update WiFi creds at runtime, persist to NVS.
  if (commandUpper.startsWith(F("SETWIFI"))) {
    auto extractQuoted = [&](const char* key) -> String {
      int idx = command.indexOf(key);
      if (idx < 0) return String();
      int eq = command.indexOf('=', idx);
      if (eq < 0) return String();
      int q1 = command.indexOf('"', eq);
      if (q1 < 0) return String();
      int q2 = command.indexOf('"', q1 + 1);
      if (q2 < 0) return String();
      return command.substring(q1 + 1, q2);
    };
    String newSsid = extractQuoted("ssid");
    String newPass = extractQuoted("pass");
    if (newSsid.length() == 0) {
      Serial.println(F("ERR SETWIFI missing ssid"));
      return;
    }
    applyNewWifiCredentials(newSsid, newPass);
    Serial.println(F("OK SETWIFI saved"));
    return;
  }
  if (commandUpper == F("ARM")) {
    motionArmed = true;
    pcaAllChannelsOff();
    Serial.println(F("OK ARMED"));
    return;
  }
  if (commandUpper == F("DISARM")) {
    motionArmed = false;
    liftToIr51Active = false;
    liftToIr51StartedMs = 0;
    stopElevatorPark();
    stopAllDispense();
    pcaAllChannelsOff();
    Serial.println(F("OK DISARMED"));
    return;
  }
  if (commandUpper == F("PINGUNO")) {
    Serial.print(F("DEBUG writing @PING to UnoSerial on TX pin "));
    Serial.println(UNO_TX_PIN);
    UnoSerial.print('@');
    UnoSerial.println(F("PING"));
    UnoSerial.flush();
    Serial.println(F(">UNO>@PING"));
    Serial.println(F("DEBUG @PING flushed"));
    return;
  }
  if (commandUpper == F("STOP")) {
    stopElevatorPark();
    stopAllDispense();
    return;
  }
  if (commandUpper == F("HOME")) {
    stopElevatorPark();
    homeServos();
    return;
  }
  if (commandUpper == F("HELP")) {
    printHelp();
    return;
  }
  if (commandUpper == F("PULSEDEBUG")) {
    pulseDebugEnabled = !pulseDebugEnabled;
    Serial.print(F("OK PULSEDEBUG="));
    Serial.println(pulseDebugEnabled ? F("1") : F("0"));
    Serial.print(F("RAW billPin="));
    Serial.print(digitalRead(BILL_PIN));
    Serial.print(F(" coinPin="));
    Serial.println(digitalRead(COIN_PIN));
    return;
  }
  if (commandUpper == F("DIAGBILL")) {
    startInputDiag(BILL_PIN, "bill", 10000);
    return;
  }
  if (commandUpper == F("DIAGCOIN")) {
    startInputDiag(COIN_PIN, "coin", 10000);
    return;
  }

  if (commandUpper == F("M4IR")) {
    const int raw = digitalRead(M4_IR_PIN);
    Serial.print(F("M4IR raw="));
    Serial.print(raw);
    Serial.print(F(" baseline="));
    Serial.print(m4IrBaselineLevel);
    Serial.print(F(" detected="));
    Serial.print(isM4IrDetected() ? F("1") : F("0"));
    Serial.println();
    return;
  }

  if (commandUpper == F("SENSORSCAN")) {
    Serial.println(F("=== IR SENSOR STATUS ==="));
    Serial.println(F("Elevator Bill Path (IR5):"));
    const int8_t faceSlot = currentIr5FaceSlot();
    Serial.print(F("  FACE_LOCK slot="));
    if (faceSlot >= 0) {
      Serial.println(faceSlot + 1);
    } else {
      Serial.println(F("NONE/AMBIG"));
    }
    for (uint8_t i = 0; i < 5; ++i) {
      Serial.print(F("  IR5-"));
      Serial.print(i + 1);
      Serial.print(F(" (slot "));
      Serial.print(i + 1);
      Serial.print(F("): raw="));
      Serial.print(digitalRead(IR5_PINS[i]));
      Serial.print(F(" rawDetected="));
      Serial.print(isIr5RawDetected(i) ? F("YES") : F("NO"));
      Serial.print(F(" polarity="));
      Serial.print(ir5ActiveLow[i] ? F("LOW") : F("HIGH"));
      Serial.print(F(" detected="));
      Serial.println(isIr5Detected(i) ? F("YES") : F("NO"));
    }
    Serial.println(F("Motor 4 (Coin):"));
    Serial.print(F("  M4IR: raw="));
    Serial.print(digitalRead(M4_IR_PIN));
    Serial.print(F(" detected="));
    Serial.println(isM4IrDetected() ? F("YES") : F("NO"));
    Serial.println(F("======================"));
    return;
  }

  if (commandUpper.startsWith(F("IR5POL "))) {
    int slot = 0;
    char mode[12] = {0};
    if (sscanf(command.c_str() + 7, "%d %11s", &slot, mode) != 2) {
      Serial.println(F("ERR format: IR5POL <slot1to5> <LOW|HIGH|0|1>"));
      return;
    }
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR IR5POL slot 1..5"));
      return;
    }

    String modeToken = String(mode);
    modeToken.trim();
    modeToken.toUpperCase();

    bool activeLow = false;
    bool valid = true;
    if (modeToken == F("LOW") || modeToken == F("1")) {
      activeLow = true;
    } else if (modeToken == F("HIGH") || modeToken == F("0")) {
      activeLow = false;
    } else {
      valid = false;
    }

    if (!valid) {
      Serial.println(F("ERR IR5POL mode LOW|HIGH|0|1"));
      return;
    }

    const uint8_t idx = static_cast<uint8_t>(slot - 1);
    ir5ActiveLow[idx] = activeLow;
    const bool initial = isIr5RawDetected(idx);
    ir5RawLastState[idx] = initial;
    ir5StableState[idx] = initial;
    ir5RawChangedMs[idx] = millis();
    ir5PreferredSlot = idx;

    Serial.print(F("OK IR5POL slot="));
    Serial.print(slot);
    Serial.print(F(" polarity="));
    Serial.println(activeLow ? F("LOW") : F("HIGH"));
    return;
  }

  // M4COIL a b c d — drive raw 0/1 to IN1..IN4 for hardware verification
  if (commandUpper.startsWith(F("M4COIL "))) {
    int v[4] = {0, 0, 0, 0};
    sscanf(command.c_str() + 7, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
    writeMotor4(v[0] & 1, v[1] & 1, v[2] & 1, v[3] & 1);
    Serial.print(F("OK M4COIL "));
    Serial.print(v[0]); Serial.print(' ');
    Serial.print(v[1]); Serial.print(' ');
    Serial.print(v[2]); Serial.print(' ');
    Serial.println(v[3]);
    return;
  }

  // M4PINMAP p0 p1 p2 p3 — remap which step-sequence column drives each physical IN pin
  // Default: 0 1 2 3  Try: 0 2 1 3 / 0 3 2 1 / 1 0 3 2 / etc.
  if (commandUpper.startsWith(F("M4PINMAP "))) {
    int p[4] = {0, 1, 2, 3};
    sscanf(command.c_str() + 9, "%d %d %d %d", &p[0], &p[1], &p[2], &p[3]);
    bool valid = true;
    for (int i = 0; i < 4 && valid; ++i) if (p[i] < 0 || p[i] > 3) valid = false;
    if (!valid) { Serial.println(F("ERR M4PINMAP values must be 0-3")); return; }
    for (int i = 0; i < 4; ++i) m4PinMap[i] = static_cast<uint8_t>(p[i]);
    motor4.sequenceIndex = 0;
    Serial.print(F("OK M4PINMAP "));
    Serial.print(m4PinMap[0]); Serial.print(' ');
    Serial.print(m4PinMap[1]); Serial.print(' ');
    Serial.print(m4PinMap[2]); Serial.print(' ');
    Serial.println(m4PinMap[3]);
    return;
  }

  if (commandUpper.startsWith(F("DISPENSE "))) {
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

  if (commandUpper.startsWith(F("PAYOUT "))) {
    const int amount = command.substring(7).toInt();
    handlePayoutCommand(amount);
    return;
  }

  if (commandUpper.startsWith(F("WITHDRAW"))) {
    int amount = 0;
    int coin1 = 0, coin5 = 0, coin10 = 0, coin20 = 0;
    int bill20 = 0, bill50 = 0, bill100 = 0, bill500 = 0, bill1000 = 0;
    
    // Parse amount
    int firstDigit = -1;
    for (int i = 0; i < command.length(); ++i) {
      const char ch = command.charAt(i);
      if (ch >= '0' && ch <= '9') {
        firstDigit = i;
        break;
      }
    }
    if (firstDigit >= 0) {
      amount = command.substring(firstDigit).toInt();
    }
    
    // Parse coin/bill breakdown: "WITHDRAW 20 coin20=1 bill50=1 ..."
    if (command.indexOf(F("coin1=")) >= 0) coin1 = sscanfValue(command, "coin1=");
    if (command.indexOf(F("coin5=")) >= 0) coin5 = sscanfValue(command, "coin5=");
    if (command.indexOf(F("coin10=")) >= 0) coin10 = sscanfValue(command, "coin10=");
    if (command.indexOf(F("coin20=")) >= 0) coin20 = sscanfValue(command, "coin20=");
    if (command.indexOf(F("bill20=")) >= 0) bill20 = sscanfValue(command, "bill20=");
    if (command.indexOf(F("bill50=")) >= 0) bill50 = sscanfValue(command, "bill50=");
    if (command.indexOf(F("bill100=")) >= 0) bill100 = sscanfValue(command, "bill100=");
    if (command.indexOf(F("bill500=")) >= 0) bill500 = sscanfValue(command, "bill500=");
    if (command.indexOf(F("bill1000=")) >= 0) bill1000 = sscanfValue(command, "bill1000=");
    
    if (amount <= 0) {
      amount = 20;
      Serial.println(F("WARN WITHDRAW amount missing; defaulting to 20"));
    }
    
    // Bills first (largest denominations first), then defer coins until bills
    // finish so the two motor systems never run at the same time.
    int coinTotal = coin1 * 1 + coin5 * 5 + coin10 * 10 + coin20 * 20;
    const bool hasBills = (bill20 > 0 || bill50 > 0 || bill100 > 0 || bill500 > 0 || bill1000 > 0);

    if (hasBills) {
      uint8_t counts[5] = {
        static_cast<uint8_t>(bill20),
        static_cast<uint8_t>(bill50),
        static_cast<uint8_t>(bill100),
        static_cast<uint8_t>(bill500),
        static_cast<uint8_t>(bill1000)
      };
      if (coinTotal > 0) {
        pendingWithdraw.active = true;
        pendingWithdraw.coinAmount = coinTotal;
        Serial.print(F("WITHDRAW coins DEFERRED until bills finish amount="));
        Serial.println(coinTotal);
      }
      startWithdrawJobWithCounts(counts);
    } else if (coinTotal > 0) {
      Serial.print(F("WITHDRAW coins-only coin1="));
      Serial.print(coin1); Serial.print(F(" coin5="));
      Serial.print(coin5); Serial.print(F(" coin10="));
      Serial.print(coin10); Serial.print(F(" coin20="));
      Serial.print(coin20); Serial.print(F(" total="));
      Serial.println(coinTotal);
      handlePayoutCommand(coinTotal);
    } else if (coinTotal == 0) {
      // No coins and no bills - error
      Serial.println(F("ERR WITHDRAW empty breakdown"));
    }
    return;
  }

  if (commandUpper.startsWith(F("ROUTE "))) {
    const int slot = command.substring(6).toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR route slot 1..5"));
      return;
    }
    queueBillSlot(static_cast<uint8_t>(slot - 1));
    return;
  }

  if (commandUpper.startsWith(F("STORAGE "))) {
    const String payload = command.substring(8);
    const int split = payload.indexOf(' ');
    if (split < 0) {
      Serial.println(F("ERR format: STORAGE <slot1to5|ALL> <FORWARD|BACKWARD|STOP|cmd0to180>"));
      return;
    }

    String slotToken = payload.substring(0, split);
    String cmdToken = payload.substring(split + 1);
    slotToken.trim();
    cmdToken.trim();

    String cmdUpper = cmdToken;
    cmdUpper.toUpperCase();
    const bool isForward = (cmdUpper == F("FORWARD"));
    const bool isBackward = (cmdUpper == F("BACKWARD"));
    const bool isStop = (cmdUpper == F("STOP"));

    int cmd = -1;
    if (isForward) {
      cmd = SPINNER_RUN_CMD;
    } else if (isBackward) {
      cmd = SPINNER_WITHDRAW_CMD;
    } else if (!isStop) {
      bool numeric = (cmdToken.length() > 0);
      for (uint16_t i = 0; i < cmdToken.length() && numeric; ++i) {
        const char ch = cmdToken.charAt(i);
        if (ch < '0' || ch > '9') {
          numeric = false;
        }
      }
      if (!numeric) {
        Serial.println(F("ERR storage cmd use FORWARD/BACKWARD/STOP or 0..180"));
        return;
      }
      cmd = cmdToken.toInt();
      if (cmd < 0 || cmd > 180) {
        Serial.println(F("ERR storage cmd 0..180"));
        return;
      }
    }

    String slotUpper = slotToken;
    slotUpper.toUpperCase();

    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (STORAGE)"));
    }

    if (isStop) {
      if (slotUpper == F("ALL")) {
        for (uint8_t slot = 0; slot < 5; ++slot) {
          stopStorageMotor(slot);
        }
        Serial.println(F("OK STORAGE all STOP"));
        return;
      }

      const int slot = slotToken.toInt();
      if (slot < 1 || slot > 5) {
        Serial.println(F("ERR storage slot 1..5"));
        return;
      }

      stopStorageMotor(static_cast<uint8_t>(slot - 1));
      Serial.print(F("OK STORAGE slot="));
      Serial.print(slot);
      Serial.println(F(" STOP"));
      return;
    }

    if (slotUpper == F("ALL")) {
      spinAllStorageMotors(static_cast<uint8_t>(cmd));
      return;
    }

    const int slot = slotToken.toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR storage slot 1..5"));
      return;
    }

    spinStorageMotor(static_cast<uint8_t>(slot - 1), static_cast<uint8_t>(cmd));
    return;
  }

  if (commandUpper.startsWith(F("STORAGETEST "))) {
    const int slot = command.substring(12).toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR storagetest slot 1..5"));
      return;
    }
    runStorageTest(static_cast<uint8_t>(slot - 1));
    return;
  }

  if (commandUpper == F("STORAGECONF")) {
    printStorageConfig();
    return;
  }

  if (commandUpper.startsWith(F("STORAGEMAP "))) {
    int slot = 0;
    int channel = 0;
    if (sscanf(command.c_str() + 11, "%d %d", &slot, &channel) != 2) {
      Serial.println(F("ERR format: STORAGEMAP <slot1to5> <channel0to4>"));
      return;
    }
    if (slot < 1 || slot > 5 || channel < 0 || channel > 4) {
      Serial.println(F("ERR STORAGEMAP slot 1..5 channel 0..4"));
      return;
    }
    storageChannelMap[static_cast<uint8_t>(slot - 1)] = static_cast<uint8_t>(channel);
    Serial.print(F("OK STORAGEMAP slot="));
    Serial.print(slot);
    Serial.print(F(" channel="));
    Serial.println(channel);
    return;
  }

  if (commandUpper.startsWith(F("STORAGEINV "))) {
    int slot = 0;
    int invert = 0;
    if (sscanf(command.c_str() + 11, "%d %d", &slot, &invert) != 2) {
      Serial.println(F("ERR format: STORAGEINV <slot1to5> <0|1>"));
      return;
    }
    if (slot < 1 || slot > 5 || (invert != 0 && invert != 1)) {
      Serial.println(F("ERR STORAGEINV slot 1..5 invert 0|1"));
      return;
    }
    storageDirectionInverted[static_cast<uint8_t>(slot - 1)] = (invert != 0);
    Serial.print(F("OK STORAGEINV slot="));
    Serial.print(slot);
    Serial.print(F(" inverted="));
    Serial.println(invert);
    return;
  }

  if (commandUpper.startsWith(F("STORAGESTOP"))) {
    String slotToken = command.substring(11);
    slotToken.trim();
    if (slotToken.length() == 0) {
      slotToken = F("ALL");
    }

    String slotUpper = slotToken;
    slotUpper.toUpperCase();
    if (slotUpper == F("ALL")) {
      for (uint8_t slot = 0; slot < 5; ++slot) {
        stopStorageMotor(slot);
      }
      Serial.println(F("OK STORAGESTOP all"));
      return;
    }

    const int slot = slotToken.toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR storage slot 1..5"));
      return;
    }

    stopStorageMotor(static_cast<uint8_t>(slot - 1));
    Serial.print(F("OK STORAGESTOP slot="));
    Serial.println(slot);
    return;
  }

  if (commandUpper == F("LIFTUP")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    if (!validateLiftDirection(ELEVATOR_LIFT_UP_CMD, 0)) {
      Serial.println(F("ERR already at top limit"));
      return;
    }
    setLiftCommand(ELEVATOR_LIFT_UP_CMD);
    Serial.println(F("OK LIFTUP"));
    return;
  }

  if (commandUpper == F("LIFTDOWN")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    if (!validateLiftDirection(ELEVATOR_LIFT_DOWN_CMD, 4)) {
      Serial.println(F("ERR already at bottom limit"));
      return;
    }
    setLiftCommand(ELEVATOR_LIFT_DOWN_CMD);
    Serial.println(F("OK LIFTDOWN"));
    return;
  }

  if (commandUpper == F("LIFTSTOP")) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    stopElevatorPark();
    setLiftCommand(SERVO_STOP_CMD);
    Serial.println(F("OK LIFTSTOP"));
    return;
  }

  if (commandUpper == F("LIFTTO1")) {
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

  if (commandUpper.startsWith(F("IR5-")) && commandUpper.length() == 5) {
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

  if (commandUpper == F("LIFTTEST")) {
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (LIFTTEST)"));
    }
    runLiftTest();
    return;
  }

  if (commandUpper.startsWith(F("SERVO "))) {
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

  if (commandUpper.startsWith(F("TESTDEPOSIT "))) {
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

  // DEPOSIT [amount] is a no-op on the ESP. Real deposits come from the bill
  // acceptor (GPIO32) which calls postDeposit() directly. Accept the command
  // silently so stray cloud-queued DEPOSIT messages don't trigger ERR loops
  // (which previously crashed the panic handler and rebooted the device).
  if (commandUpper.startsWith(F("DEPOSIT"))) {
    Serial.println(F("OK DEPOSIT (no-op; insert bill into acceptor)"));
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
  if (line.startsWith(F("OK DISPENSE"))) {
    unoOnline = true;
    return;
  }
  if (line.startsWith(F("DONE motor="))) {
    unoOnline = true;
    remoteTaskActive = false;
    remoteTaskSentMs = 0;
    if (taskCount > 0 && taskQueue[0].motor >= 1 && taskQueue[0].motor <= 3) {
      popQueueFront();
    }
    return;
  }
  if (line.startsWith(F("ERR motor=")) || line == F("OK STOP")) {
    unoOnline = true;
    remoteTaskActive = false;
    remoteTaskSentMs = 0;
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
  pinMode(M4_IR_PIN, INPUT);
  pinMode(BILL_PIN, INPUT_PULLUP);
  pinMode(COIN_PIN, INPUT_PULLUP);
  for (uint8_t index = 0; index < 5; ++index) {
    pinMode(IR5_PINS[index], INPUT_PULLUP);
    const bool initial = isIr5RawDetected(index);
    ir5RawLastState[index] = initial;
    ir5StableState[index] = initial;
    ir5RawChangedMs[index] = millis();
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

  // Initiate Wi-Fi connection on boot. ensureWifi() handles primary +
  // fallback (CASHWIFI/CASH12345!) automatically; loop() will keep retrying
  // every 5 s if the first attempt times out.
  loadWifiCredentials();
  ensureWifi();

  UnoSerial.print('@');
  UnoSerial.println(F("PING"));
  Serial.println(F(">UNO>@PING"));
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
  // Uno watchdog: if a DISPENSE was sent and no DONE/ERR came back within
  // DISPENSE_TIMEOUT_MS, log + clear so subsequent tasks can be sent.
  if (remoteTaskActive && remoteTaskSentMs > 0 &&
      millis() - remoteTaskSentMs > DISPENSE_TIMEOUT_MS) {
    Serial.print(F("WARN uno timeout motor="));
    Serial.print(remoteTaskMotor);
    Serial.print(F(" count="));
    Serial.print(remoteTaskCount);
    Serial.print(F(" elapsedMs="));
    Serial.println(millis() - remoteTaskSentMs);
    remoteTaskActive = false;
    remoteTaskSentMs = 0;
    if (taskCount > 0 && taskQueue[0].motor == remoteTaskMotor) {
      popQueueFront();
    }
  }
  startNextQueuedTask();
  servicePendingWithdraw();
  serviceWithdrawJob();
  pollRemoteCommands();

  const unsigned long nowMs = millis();
  if (LOOP_HEARTBEAT_VERBOSE && (nowMs - lastLoopHeartbeatMs >= LOOP_HEARTBEAT_MS)) {
    lastLoopHeartbeatMs = nowMs;
    Serial.print(F("ALIVE wifi="));
    Serial.print(WiFi.status());
    Serial.print(F(" ip="));
    Serial.print(WiFi.localIP());
    Serial.print(F(" q="));
    Serial.print(taskCount);
    Serial.print(F(" withdraw="));
    Serial.print(withdrawJob.active ? F("1") : F("0"));
    Serial.print(F(" pollFail="));
    Serial.println(commandPollFailures);
  }
  if (STATUS_VERBOSE_LOG && (nowMs - lastStatusPrintMs >= STATUS_PRINT_MS)) {
    lastStatusPrintMs = nowMs;
    printStatus();
  }
}