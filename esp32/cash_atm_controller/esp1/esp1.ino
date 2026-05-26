#include <Wire.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

constexpr long USB_SERIAL_BAUD = 115200;
constexpr long RASPI_UART_BAUD = 115200;
constexpr uint8_t RASPI_UART_RX_PIN = 16;
constexpr uint8_t RASPI_UART_TX_PIN = 17;
constexpr const char* WIFI_SSID = "CASHWIFI";
constexpr const char* WIFI_PASSWORD = "CASH12345!";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr bool CLOUD_API_ENABLED = true;
constexpr bool LEGACY_USB_BRIDGE_COMPAT = false;
constexpr unsigned long CLOUD_COMMAND_POLL_INTERVAL_MS = 500;
constexpr const char* CLOUD_DEPOSIT_API = "https://cashmv.up.railway.app/api/deposit";
constexpr const char* CLOUD_COMMAND_API = "https://cashmv.up.railway.app/api/command?consume=true";
constexpr const char* CLOUD_DEVICE_STATUS_API = "https://cashmv.up.railway.app/api/device/status";
constexpr bool STEPPERS_ENABLED = true;
constexpr bool SERVOS_ENABLED = true;
constexpr bool MOTION_ARMED_DEFAULT = true;
// IR5 sensors are wired directly to ESP32 GPIOs and used by the lift routing logic.
// Keep auto-run disabled on boot so startup is deterministic.
constexpr bool AUTO_LIFT_TO_IR5_1_ON_BOOT = true;
// Master switch for IR5 level sensors (slots 1..5).
constexpr bool IR5_SENSORS_ENABLED = true;
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
// Host-side remote-task watchdog. Set 0 to disable timeout and wait for
// explicit DONE/ERR from ESP2.
constexpr unsigned long DISPENSE_TIMEOUT_MS = 0;
constexpr unsigned long STATUS_PRINT_MS = 1000;
constexpr bool STATUS_VERBOSE_LOG = false;
constexpr unsigned long PULSE_MIN_EDGE_GAP_MS = 20;  // Polled-loop default; bill ISR uses its own value below.
constexpr unsigned long PULSE_MIN_WIDTH_MS = 15;
constexpr unsigned long PULSE_MAX_WIDTH_MS = 350;
constexpr unsigned long COIN_IDLE_GAP_MS = 500;  // Increased from 250 for noise tolerance
constexpr unsigned long BILL_IDLE_GAP_MS = 900;  // Slow pulse mode needs longer gap

constexpr uint8_t M4_IN1_PIN = 18;
constexpr uint8_t M4_IN2_PIN = 19;
constexpr uint8_t M4_IN3_PIN = 23;
constexpr uint8_t M4_IN4_PIN = 27;  // moved from GPIO13 (unreliable); GPIO27 freed from IR4 (now on Uno A3)
constexpr uint8_t M4_IR_PIN = 36;   // local IR for motor 4 (input-only)

constexpr uint8_t BILL_PIN = 32;
constexpr uint8_t COIN_PIN = 14;
// IR5 slots 1..5. Chosen to avoid overlap with the PCA9685 I2C pins.
constexpr uint8_t IR5_PINS[] = {34, 35, 33, 25, 26};

constexpr uint8_t SERVO_CHANNEL_COUNT = 7;
constexpr uint16_t SERVO_PWM_MIN = 110;
constexpr uint16_t SERVO_PWM_MAX = 510;
// PCA9685 register map/constants for the 7 servo channels on I2C address 0x40.
constexpr uint8_t PCA9685_ADDRESS = 0x40;
constexpr uint8_t PCA9685_MODE1 = 0x00;
constexpr uint8_t PCA9685_MODE2 = 0x01;
constexpr uint8_t PCA9685_PRESCALE = 0xFE;
constexpr uint8_t PCA9685_LED0_ON_L = 0x06;
constexpr bool USE_PCA9685 = false;

// Direct ESP32 GPIO map for channels 0..6.
// CH0..CH4 are the 5 storage motors (no PCA required).
// CH5/CH6 are optional extra channels for lift/out paths.
constexpr uint8_t SERVO_GPIO_PINS[SERVO_CHANNEL_COUNT] = {
  4,   // CH0 storage spinner 1
  5,   // CH1 storage spinner 2
  13,  // CH2 storage spinner 3
  21,  // CH3 storage spinner 4
  22,  // CH4 storage spinner 5
  12,  // CH5 optional channel
  15   // CH6 optional channel
};
constexpr int SERVO_PULSE_MIN_US = 500;   // matches angle 0
constexpr int SERVO_PULSE_MAX_US = 2500;  // matches angle 180
Servo channelServos[SERVO_CHANNEL_COUNT];
bool channelServoAttached[SERVO_CHANNEL_COUNT] = {false, false, false, false, false, false, false};

// PCA9685 channel assignment from your thesis hardware.
constexpr uint8_t SPINNER_CHANNEL_MIN = 0;   // CH0..CH4 storage spinner servos
constexpr uint8_t SPINNER_CHANNEL_MAX = 4;
constexpr uint8_t STORAGE_SLOT_COUNT = 5;
constexpr uint8_t ELEVATOR_LIFT_CHANNEL = 6; // CH6 elevator lift servo
constexpr uint8_t ELEVATOR_OUT_CHANNEL = 5;  // CH5 elevator output spinner
constexpr bool LIFT_DIRECTION_INVERTED = true;

// 360 servo commands: 90=stop, >90 rotate one direction, <90 rotate opposite direction.
constexpr uint8_t SERVO_STOP_CMD = 90;
constexpr uint8_t SPINNER_RUN_CMD = 180;
// Reverse direction to eject bills from storage during withdrawal (max speed).
constexpr uint8_t SPINNER_WITHDRAW_CMD = 0;
constexpr uint8_t STORAGE_FORWARD_CMD = SPINNER_RUN_CMD;
constexpr uint8_t STORAGE_BACKWARD_CMD = SPINNER_WITHDRAW_CMD;

uint8_t storageChannelMap[STORAGE_SLOT_COUNT] = {0, 1, 2, 3, 4};
bool storageDirectionInverted[STORAGE_SLOT_COUNT] = {false, true, true, true, true};
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
constexpr unsigned long SERVO_TEST_DEFAULT_MS = 3000;
constexpr unsigned long SERVO_TEST_MIN_MS = 200;
constexpr unsigned long SERVO_TEST_MAX_MS = 120000;
// Withdrawal timing per storage spinner slot index (0..4 => slot 1..5).
// Per user calibration:
// M1: F1500 B300, M2: F1500 B300, M3: F1500 B300, M4: F1000 B300, M5: F1500 B300.
constexpr unsigned long WITHDRAW_FORWARD_MS_BY_SLOT[STORAGE_SLOT_COUNT] = {1500, 1500, 900, 1000, 1500};
constexpr unsigned long WITHDRAW_REVERSE_MS_BY_SLOT[STORAGE_SLOT_COUNT] = {300, 300, 300, 300, 300};
constexpr unsigned long WITHDRAW_INTER_BILL_GAP_MS = 600;  // pause between bills
constexpr unsigned long WITHDRAW_TOTAL_TIMEOUT_MS = 120000; // 2-min safety timeout
constexpr unsigned long JOB_MAX_RUNTIME_MS = 900000;        // 15-min motor safety cap

inline unsigned long withdrawForwardMsForSlot(uint8_t slotIndex) {
  if (slotIndex < STORAGE_SLOT_COUNT) {
    return WITHDRAW_FORWARD_MS_BY_SLOT[slotIndex];
  }
  return WITHDRAW_FORWARD_MS_BY_SLOT[0];
}

inline unsigned long withdrawReverseMsForSlot(uint8_t slotIndex) {
  if (slotIndex < STORAGE_SLOT_COUNT) {
    return WITHDRAW_REVERSE_MS_BY_SLOT[slotIndex];
  }
  return WITHDRAW_REVERSE_MS_BY_SLOT[0];
}

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

// Full-step sequence keeps two coils energized each step for higher torque.
constexpr uint8_t FULL_STEP_DUAL[4][4] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
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

// Set true only for legacy acceptors that report 2x the configured value.
constexpr bool BILL_AMOUNT_HALVE_AFTER_MAP = false;

constexpr unsigned long LOOP_HEARTBEAT_MS = 3000;
constexpr bool LOOP_HEARTBEAT_VERBOSE = false;

struct MotorState {
  int sequenceIndex;
};

// Migration path: run DISPENSE motors 1..4 directly on ESP32 using
// continuous-rotation servos (360 deg) instead of Uno steppers.
// Keep disabled by default for dual-ESP architecture (main ESP32 + coin ESP32).
// Set true only when this SAME board directly drives the coin-dispense servos.
constexpr bool LOCAL_COIN_SERVO_MODE = false;
constexpr uint8_t DISPENSE_MOTOR_COUNT = 4;
constexpr uint8_t INVALID_GPIO_PIN = 255;

struct CoinServoConfig {
  uint8_t servoPin;
  uint8_t irPin;
  bool irActiveLow;
  int forwardUs;
  int reverseUs;
  int stopUs;
  unsigned long startupSettleMs;
  unsigned long timeoutMs;
};

// TODO: Replace INVALID_GPIO_PIN with your actual wiring.
CoinServoConfig coinServoConfig[DISPENSE_MOTOR_COUNT] = {
  {INVALID_GPIO_PIN, INVALID_GPIO_PIN, true, 1700, 1300, 1500, 300, JOB_MAX_RUNTIME_MS},
  {INVALID_GPIO_PIN, INVALID_GPIO_PIN, true, 1700, 1300, 1500, 300, JOB_MAX_RUNTIME_MS},
  {INVALID_GPIO_PIN, INVALID_GPIO_PIN, true, 1700, 1300, 1500, 300, JOB_MAX_RUNTIME_MS},
  {INVALID_GPIO_PIN, INVALID_GPIO_PIN, true, 1700, 1300, 1500, 300, JOB_MAX_RUNTIME_MS},
};

struct LocalCoinServoJob {
  bool active;
  uint8_t motor;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  unsigned long startedMs;
  unsigned long settleUntilMs;
};

constexpr unsigned long M4_FORWARD_RUN_MS  = 15000;
constexpr unsigned long M4_BACKWARD_RUN_MS = 10000;
constexpr unsigned long M4_STEP_INTERVAL_US = 1500;
constexpr unsigned long M4_DETECT_HOLD_MS = 30;
constexpr unsigned long M4_DETECT_PULSE_MIN_MS = 8;
constexpr uint8_t M4_BASELINE_SAMPLES = 16;
constexpr bool M4_ENABLE_CYCLE_FALLBACK = false;
constexpr bool M4_USE_FULLSTEP_HIGH_TORQUE = true;

struct LocalDispenseJob {
  bool active;
  uint8_t requestedCount;
  uint8_t dispensedCount;
  bool sensorArmed;
  bool goingForward;
  bool sawDetectThisCycle;
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
Servo coinDispenseServos[DISPENSE_MOTOR_COUNT];
bool coinDispenseServoAttached[DISPENSE_MOTOR_COUNT] = {false, false, false, false};
LocalCoinServoJob coinServoJob = {};
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
bool esp2Online = false;
bool remoteTaskActive = false;
unsigned long remoteTaskSentMs = 0;
uint8_t remoteTaskMotor = 0;
uint8_t remoteTaskCount = 0;
uint8_t m4PinMap[4] = {0, 1, 2, 3}; // maps step columns → IN1/IN2/IN3/IN4 (try different orderings to find working wiring)
int m4IrBaselineLevel = HIGH;
volatile bool m4IrLatchedDetect = false;
volatile bool m4IrMonitorActive = false;
bool motionArmed = MOTION_ARMED_DEFAULT;
bool pulseDebugEnabled = false;
InputDiag inputDiag = {false, 0, nullptr, true, 0, 0, 0, 0};
uint8_t currentLiftCommand = SERVO_STOP_CMD;
String usbCommandBuffer;
String esp2LineBuffer;
unsigned long lastStatusPrintMs = 0;
unsigned long lastLoopHeartbeatMs = 0;
unsigned long lastCloudCommandPollMs = 0;

HardwareSerial RaspiSerial(2);
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
bool moveLiftDirectToSlotInstantStop(uint8_t slot);
void startLiftToIr51Sequence(const __FlashStringHelper* reason);
void handleUsbCommand(String command);
void handleEsp2Line(const String& line);
bool startLocalCoinServoJob(uint8_t motor, uint8_t count);
void serviceLocalCoinServoJob();

// ===== PCA9685 backend =====
// Production servo control is driven by PCA9685 channel outputs (0..6) over I2C.
void attachChannelServo(uint8_t channel) {
  if (channel >= SERVO_CHANNEL_COUNT) return;
  if (channelServoAttached[channel]) return;
  channelServos[channel].setPeriodHertz(50);
  channelServos[channel].attach(SERVO_GPIO_PINS[channel], SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  channelServoAttached[channel] = true;
}

void detachChannelServo(uint8_t channel) {
  if (channel >= SERVO_CHANNEL_COUNT) return;
  if (!channelServoAttached[channel]) return;
  channelServos[channel].writeMicroseconds(1500); // neutral/stop briefly
  channelServos[channel].detach();
  channelServoAttached[channel] = false;
}

void pcaWriteRegister(uint8_t reg, uint8_t value) {
  if (!USE_PCA9685) {
    (void)reg;
    (void)value;
    return;
  }
  Wire.beginTransmission(PCA9685_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t pcaReadRegister(uint8_t reg) {
  if (!USE_PCA9685) {
    (void)reg;
    return 0;
  }
  Wire.beginTransmission(PCA9685_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return 0;
  }
  Wire.requestFrom(PCA9685_ADDRESS, static_cast<uint8_t>(1));
  if (Wire.available()) {
    return Wire.read();
  }
  return 0;
}

void pcaSetPwm(uint8_t channel, uint16_t onCount, uint16_t offCount) {
  if (channel >= SERVO_CHANNEL_COUNT) return;

  if (!USE_PCA9685) {
    if (onCount == 0 && offCount == 0) {
      detachChannelServo(channel);
      return;
    }

    const uint16_t ticks = static_cast<uint16_t>(offCount & 0x0FFF);
    uint32_t pulseUs = (static_cast<uint32_t>(ticks) * 20000UL) / 4096UL;
    if (pulseUs < 500UL) pulseUs = 500UL;
    if (pulseUs > 2500UL) pulseUs = 2500UL;

    attachChannelServo(channel);
    channelServos[channel].writeMicroseconds(static_cast<int>(pulseUs));
    return;
  }

  const uint8_t baseReg = PCA9685_LED0_ON_L + static_cast<uint8_t>(4 * channel);
  pcaWriteRegister(baseReg + 0, onCount & 0xFF);
  pcaWriteRegister(baseReg + 1, (onCount >> 8) & 0x0F);
  pcaWriteRegister(baseReg + 2, offCount & 0xFF);
  pcaWriteRegister(baseReg + 3, (offCount >> 8) & 0x0F);
}

bool pcaIsPresent() {
  if (!USE_PCA9685) {
    return true;
  }
  Wire.beginTransmission(PCA9685_ADDRESS);
  return Wire.endTransmission() == 0;
}

void pcaInit50Hz() {
  if (!USE_PCA9685) {
    return;
  }
  // Force totem-pole outputs (OUTDRV=1) for servo signal compatibility.
  // Some boards/power-up states can leave outputs in open-drain behavior.
  pcaWriteRegister(PCA9685_MODE2, 0x04);

  // Enter sleep before prescale write.
  const uint8_t oldMode = pcaReadRegister(PCA9685_MODE1);
  const uint8_t sleepMode = static_cast<uint8_t>((oldMode & 0x7F) | 0x10);
  pcaWriteRegister(PCA9685_MODE1, sleepMode);

  // 25MHz / (4096 * 50Hz) - 1 = 121
  pcaWriteRegister(PCA9685_PRESCALE, 121);
  pcaWriteRegister(PCA9685_MODE1, oldMode);
  delay(5);
  // Auto-increment enabled.
  pcaWriteRegister(PCA9685_MODE1, static_cast<uint8_t>(oldMode | 0x20));
}

void pcaAllChannelsOff() {
  for (uint8_t ch = 0; ch < SERVO_CHANNEL_COUNT; ++ch) {
    pcaSetPwm(ch, 0, 0);
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
  // IR5-4 is prone to distant/false triggers. Use a stricter confidence gate:
  // 9 fast samples and require at least 8 hits before reporting detected.
  // This reduces sensitivity without changing global IR polarity settings.
  if (slot == 3) {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < 9; ++i) {
      const int s = digitalRead(IR5_PINS[slot]);
      const bool det = ir5ActiveLow[slot] ? (s == LOW) : (s == HIGH);
      if (det) ++hits;
      delayMicroseconds(150);
    }
    return hits >= 8;
  }

  // IR5-5 keeps the previous noise-tolerant majority vote behavior.
  if (slot == 4) {
    uint8_t hits = 0;
    for (uint8_t i = 0; i < 7; ++i) {
      const int s = digitalRead(IR5_PINS[slot]);
      const bool det = ir5ActiveLow[slot] ? (s == LOW) : (s == HIGH);
      if (det) ++hits;
      delayMicroseconds(150);
    }
    return hits >= 4;
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

int sampleM4IrBaselineLevel() {
  uint8_t highCount = 0;
  for (uint8_t i = 0; i < M4_BASELINE_SAMPLES; ++i) {
    if (digitalRead(M4_IR_PIN) == HIGH) {
      ++highCount;
    }
    delayMicroseconds(200);
  }
  return (highCount >= (M4_BASELINE_SAMPLES / 2)) ? HIGH : LOW;
}

void IRAM_ATTR onM4IrPinChange() {
  if (!m4IrMonitorActive) {
    return;
  }
  const int raw = digitalRead(M4_IR_PIN);
  if (raw != m4IrBaselineLevel) {
    m4IrLatchedDetect = true;
  }
}

bool isCoinServoConfigured(uint8_t motorIndex) {
  if (motorIndex >= DISPENSE_MOTOR_COUNT) {
    return false;
  }
  return coinServoConfig[motorIndex].servoPin != INVALID_GPIO_PIN &&
         coinServoConfig[motorIndex].irPin != INVALID_GPIO_PIN;
}

void attachCoinServo(uint8_t motorIndex) {
  if (motorIndex >= DISPENSE_MOTOR_COUNT || coinDispenseServoAttached[motorIndex]) {
    return;
  }
  const CoinServoConfig& cfg = coinServoConfig[motorIndex];
  coinDispenseServos[motorIndex].setPeriodHertz(50);
  coinDispenseServos[motorIndex].attach(cfg.servoPin, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  coinDispenseServoAttached[motorIndex] = true;
}

void stopCoinServoMotor(uint8_t motorIndex) {
  if (motorIndex >= DISPENSE_MOTOR_COUNT) {
    return;
  }
  if (!coinDispenseServoAttached[motorIndex]) {
    return;
  }
  coinDispenseServos[motorIndex].writeMicroseconds(coinServoConfig[motorIndex].stopUs);
}

bool isCoinServoIrDetected(uint8_t motorIndex) {
  if (motorIndex >= DISPENSE_MOTOR_COUNT || !isCoinServoConfigured(motorIndex)) {
    return false;
  }
  const CoinServoConfig& cfg = coinServoConfig[motorIndex];
  const int raw = digitalRead(cfg.irPin);
  return cfg.irActiveLow ? (raw == LOW) : (raw == HIGH);
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
  if (M4_USE_FULLSTEP_HIGH_TORQUE) {
    const uint8_t index = static_cast<uint8_t>(motor4.sequenceIndex & 0x03);
    writeMotor4(
      FULL_STEP_DUAL[index][0],
      FULL_STEP_DUAL[index][1],
      FULL_STEP_DUAL[index][2],
      FULL_STEP_DUAL[index][3]
    );
    motor4.sequenceIndex = (index + 3) & 0x03;
    return;
  }

  writeMotor4(
    HALF_STEP[motor4.sequenceIndex][0],
    HALF_STEP[motor4.sequenceIndex][1],
    HALF_STEP[motor4.sequenceIndex][2],
    HALF_STEP[motor4.sequenceIndex][3]
  );
  motor4.sequenceIndex = (motor4.sequenceIndex + 7) & 0x07;
}

void stepMotor4Backward() {
  if (M4_USE_FULLSTEP_HIGH_TORQUE) {
    const uint8_t index = static_cast<uint8_t>(motor4.sequenceIndex & 0x03);
    writeMotor4(
      FULL_STEP_DUAL[index][0],
      FULL_STEP_DUAL[index][1],
      FULL_STEP_DUAL[index][2],
      FULL_STEP_DUAL[index][3]
    );
    motor4.sequenceIndex = (index + 1) & 0x03;
    return;
  }

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

bool postJsonToCloud(const char* url, const String& payload) {
  if (!CLOUD_API_ENABLED || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(2000);

  if (!http.begin(client, url)) {
    Serial.print(F("HTTP begin failed url="));
    Serial.println(url);
    return false;
  }

  http.addHeader(F("Content-Type"), F("application/json"));
  const int code = http.POST(payload);
  if (code < 200 || code >= 300) {
    Serial.print(F("HTTP POST fail code="));
    Serial.print(code);
    Serial.print(F(" url="));
    Serial.println(url);
    http.end();
    return false;
  }

  http.end();
  return true;
}

void dispatchCloudCommands(const String& jsonBody) {
  const int commandsKey = jsonBody.indexOf(F("\"commands\""));
  if (commandsKey < 0) {
    return;
  }
  const int arrayStart = jsonBody.indexOf('[', commandsKey);
  if (arrayStart < 0) {
    return;
  }

  int i = arrayStart + 1;
  while (i < jsonBody.length()) {
    while (i < jsonBody.length() &&
           (jsonBody.charAt(i) == ' ' || jsonBody.charAt(i) == '\t' ||
            jsonBody.charAt(i) == '\r' || jsonBody.charAt(i) == '\n' ||
            jsonBody.charAt(i) == ',')) {
      ++i;
    }

    if (i >= jsonBody.length() || jsonBody.charAt(i) == ']') {
      break;
    }
    if (jsonBody.charAt(i) != '"') {
      ++i;
      continue;
    }

    ++i;
    String cmd = "";
    bool escape = false;
    while (i < jsonBody.length()) {
      const char ch = jsonBody.charAt(i++);
      if (escape) {
        switch (ch) {
          case 'n': cmd += '\n'; break;
          case 'r': cmd += '\r'; break;
          case 't': cmd += '\t'; break;
          case '"': cmd += '"'; break;
          case '\\': cmd += '\\'; break;
          default: cmd += ch; break;
        }
        escape = false;
        continue;
      }
      if (ch == '\\') {
        escape = true;
        continue;
      }
      if (ch == '"') {
        break;
      }
      cmd += ch;
    }

    cmd.trim();
    if (cmd.length() > 0) {
      Serial.print(F("CLOUD CMD: "));
      Serial.println(cmd);
      handleUsbCommand(cmd);
    }
  }
}

void serviceCloudCommandPoll() {
  if (!CLOUD_API_ENABLED || WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Keep motor stepping smooth: skip blocking cloud poll while dispensing.
  if (remoteTaskActive || motor4Job.active || coinServoJob.active) {
    return;
  }

  const unsigned long nowMs = millis();
  if (nowMs - lastCloudCommandPollMs < CLOUD_COMMAND_POLL_INTERVAL_MS) {
    return;
  }
  // Stamp AFTER the call so the 500 ms gap is measured from the END of the
  // HTTP transaction, not from the start. This prevents back-to-back blocking
  // calls when Railway is slow or times out.
  lastCloudCommandPollMs = millis();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(2000);

  if (!http.begin(client, CLOUD_COMMAND_API)) {
    lastCloudCommandPollMs = millis();
    return;
  }

  const int code = http.GET();
  if (code == 200) {
    const String body = http.getString();
    dispatchCloudCommands(body);
  }
  http.end();
  lastCloudCommandPollMs = millis();
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

  String payload = F("{\"amount\":");
  payload += String(amount, 2);
  payload += F(",\"source\":\"");
  payload += source;
  payload += F("\"}");
  postJsonToCloud(CLOUD_DEPOSIT_API, payload);
}

void postWithdrawStatus(const char* state, int amount, bool active) {
  String payload = F("{\"withdrawActive\":");
  payload += (active ? F("true") : F("false"));
  payload += F(",\"withdrawState\":\"");
  payload += state;
  payload += F("\",\"withdrawAmount\":");
  payload += String(amount);
  payload += F("}");
  postJsonToCloud(CLOUD_DEVICE_STATUS_API, payload);
}

// Continuous-rotation pulse widths used by the storage spinners (CH0..CH4).
// Use a wider range so motors reliably leave neutral deadband across servo
// brands: forward=1100us, reverse=1900us, stop=1500us.
constexpr int STORAGE_SPINNER_FORWARD_US = 1100;
constexpr int STORAGE_SPINNER_REVERSE_US = 1900;
constexpr int STORAGE_SPINNER_STOP_US    = 1500;

uint16_t pcaOffCountFromPulseUs(int pulseUs) {
  if (pulseUs < 500) pulseUs = 500;
  if (pulseUs > 2500) pulseUs = 2500;
  // 20ms frame at 50Hz and 4096 ticks per frame.
  return static_cast<uint16_t>((static_cast<uint32_t>(pulseUs) * 4096UL) / 20000UL);
}

void setServoAngle(uint8_t channel, uint8_t angle) {
  if (!SERVOS_ENABLED) {
    return;
  }
  if (!motionArmed) {
    return;
  }
  if (channel >= SERVO_CHANNEL_COUNT) {
    return;
  }
  if (angle > 180) {
    angle = 180;
  }
  int pulseUs = 1500;
  // Storage spinners (CH0..CH4): translate logical angles into the slow
  // pulse widths so all spinners match spinner 1's speed.
  if (channel < STORAGE_SLOT_COUNT) {
    pulseUs = STORAGE_SPINNER_STOP_US;
    if (angle == SPINNER_RUN_CMD) {            // 180 -> forward
      pulseUs = STORAGE_SPINNER_FORWARD_US;
    } else if (angle == SPINNER_WITHDRAW_CMD) { // 0 -> reverse
      pulseUs = STORAGE_SPINNER_REVERSE_US;
    } else if (angle == SERVO_STOP_CMD) {       // 90 -> stop
      pulseUs = STORAGE_SPINNER_STOP_US;
    } else {
      // Linear interpolation for STORAGE <slot> <cmd0..180> raw values:
      // 0 -> REVERSE_US, 90 -> STOP_US, 180 -> FORWARD_US
      if (angle < SERVO_STOP_CMD) {
        pulseUs = STORAGE_SPINNER_STOP_US +
             ((int)(STORAGE_SPINNER_REVERSE_US - STORAGE_SPINNER_STOP_US) *
              (int)(SERVO_STOP_CMD - angle)) / (int)SERVO_STOP_CMD;
      } else {
        pulseUs = STORAGE_SPINNER_STOP_US +
             ((int)(STORAGE_SPINNER_FORWARD_US - STORAGE_SPINNER_STOP_US) *
              (int)(angle - SERVO_STOP_CMD)) / (int)SERVO_STOP_CMD;
      }
    }
  } else {
    pulseUs = map(angle, 0, 180, 500, 2500);
  }

  pcaSetPwm(channel, 0, pcaOffCountFromPulseUs(pulseUs));
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
  delay(withdrawForwardMsForSlot(slot));

  Serial.print(F("STORAGETEST slot="));
  Serial.print(slot + 1);
  Serial.println(F(" BACKWARD"));
  setServoAngle(storageChannelForSlot(slot), storageCommandForSlot(slot, SPINNER_WITHDRAW_CMD));
  delay(withdrawReverseMsForSlot(slot));

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
  const uint8_t finishedTargetSlot = billRoute.targetSlot;
  setLiftCommand(SERVO_STOP_CMD);
  stopElevatorOutMotor();
  stopStorageMotor(billRoute.targetSlot);
  Serial.print(F("BILL ROUTE "));
  Serial.println(result);
  popBillSlot();
  billRoute = {};

  // After each bill route cycle, always return to IR5-1 so the elevator is
  // ready to accept the next bill deposit.
  if (finishedTargetSlot != 0 && !withdrawJob.active) {
    Serial.println(F("BILL ROUTE AUTO RETURN IR5-1"));
    startLiftToIr51Sequence(F("BILL_DONE"));
  }
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

void sendEsp2Command(const String& cmd) {
  RaspiSerial.println(cmd);
  Serial.print(F("ESP2<="));
  Serial.println(cmd);
}

void startLocalMotor4(uint8_t count) {
  motor4Job.active = true;
  motor4Job.requestedCount = count;
  motor4Job.dispensedCount = 0;
  motor4.sequenceIndex = 0;
  m4IrBaselineLevel = sampleM4IrBaselineLevel();
  m4IrLatchedDetect = false;
  m4IrMonitorActive = true;
  motor4Job.sensorArmed = !isM4IrDetected();
  motor4Job.goingForward = true;
  motor4Job.sawDetectThisCycle = false;
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
  if (LOCAL_COIN_SERVO_MODE) {
    if (coinServoJob.active) {
      return;
    }
    if (!startLocalCoinServoJob(nextTask.motor, nextTask.count)) {
      popQueueFront();
    }
    return;
  }

  if (nextTask.motor == 4) {
    startLocalMotor4(nextTask.count);
    return;
  }

  String cmd = F("DISPENSE ");
  cmd += String(nextTask.motor);
  cmd += ' ';
  cmd += String(nextTask.count);
  sendEsp2Command(cmd);
  remoteTaskActive = true;
  remoteTaskSentMs = millis();
  remoteTaskMotor = nextTask.motor;
  remoteTaskCount = nextTask.count;
  Serial.print(F("SENT @DISPENSE motor="));
  Serial.print(nextTask.motor);
  Serial.print(F(" count="));
  Serial.print(nextTask.count);
  Serial.print(F(" esp2Online="));
  Serial.println(esp2Online ? F("1") : F("0"));
}

void stopAllDispense() {
  taskCount = 0;
  coinServoJob = {};
  motor4Job = {};
  remoteTaskActive = false;
  for (uint8_t index = 0; index < DISPENSE_MOTOR_COUNT; ++index) {
    stopCoinServoMotor(index);
  }
  if (LOCAL_COIN_SERVO_MODE) {
    Serial.println(F("OK STOP"));
    return;
  }
  releaseMotor4();
  sendEsp2Command(F("STOP"));
  Serial.println(F("OK STOP"));
}

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  ARM"));
  Serial.println(F("  DISARM"));
  Serial.println(F("  STATUS"));
  Serial.println(F("  PINGESP2"));
  Serial.println(F("  DISPENSE <motor 1..4> <count>"));
  Serial.println(F("  PAYOUT <amount>"));
  Serial.println(F("  WITHDRAW <amount>  (20/50/100/500/1000 denominations)"));
  Serial.println(F("  ROUTE <slot1to5>"));
  Serial.println(F("  LIFTUP"));
  Serial.println(F("  LIFTDOWN"));
  Serial.println(F("  LIFTSTOP"));
  Serial.println(F("  LIFTTO1"));
  Serial.println(F("  IR5-1 .. IR5-5"));
  Serial.println(F("  FLOORTEST <slot1to5|ALL>  (instant stop on IR detect)"));
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
  Serial.println(F("  SERVO_TEST <slot1to5|channel0to6> <FORWARD|BACKWARD> [duration]"));
  Serial.println(F("    duration ex: 10000, 10000MS, 10S, 10SEC (default 3000ms)"));
  Serial.println(F("  ELEVATOR_TEST <FORWARD|BACKWARD> [duration]"));
  Serial.println(F("  OUTPUT_TEST <FORWARD|BACKWARD> [duration]"));
  Serial.println(F("  MOTOR <channel> <cmd0to180>"));
  Serial.println(F("    90=stop, >90 one direction, <90 opposite direction"));
  Serial.println(F("  PWMUS <channel> <us500to2500>"));
  Serial.println(F("  PCAREGS [channel0to6]"));
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

bool moveLiftDirectToSlotInstantStop(uint8_t slot) {
  if (slot > 4) {
    return false;
  }

  const int8_t currentSlot = currentIr5Slot();
  if (currentSlot == static_cast<int8_t>(slot)) {
    stopElevatorPark();
    setLiftCommand(SERVO_STOP_CMD);
    Serial.print(F("OK FLOORTEST IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" already (stopped)"));
    return true;
  }

  uint8_t moveCommand = ELEVATOR_LIFT_DOWN_CMD;
  if (currentSlot >= 0) {
    moveCommand = (slot < static_cast<uint8_t>(currentSlot)) ? ELEVATOR_LIFT_UP_CMD : ELEVATOR_LIFT_DOWN_CMD;
  } else if (slot == 0) {
    moveCommand = ELEVATOR_LIFT_UP_CMD;
  }

  Serial.print(F("FLOORTEST MOVE IR5-"));
  Serial.println(slot + 1);
  if (!validateLiftDirection(moveCommand, slot)) {
    Serial.print(F("ERR FLOORTEST IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" direction invalid (at limit)"));
    return false;
  }

  if (!moveLiftToIr5Slot(slot, moveCommand, ELEVATOR_SLOT_MOVE_TIMEOUT_MS)) {
    stopElevatorPark();
    setLiftCommand(SERVO_STOP_CMD);
    Serial.print(F("ERR FLOORTEST IR5-"));
    Serial.print(slot + 1);
    Serial.println(F(" timeout"));
    return false;
  }

  // Test mode requirement: stop immediately on detection, do not engage park-hold.
  stopElevatorPark();
  setLiftCommand(SERVO_STOP_CMD);
  Serial.print(F("OK FLOORTEST IR5-"));
  Serial.print(slot + 1);
  Serial.println(F(" stopped"));
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
  Serial.print(F("STATUS esp2="));
  Serial.print(esp2Online ? F("online") : F("offline"));
  Serial.print(F(" armed="));
  Serial.print(motionArmed ? F("1") : F("0"));
  Serial.print(F(" queue="));
  Serial.print(taskCount);
  Serial.print(F(" remote="));
  Serial.print(remoteTaskActive ? F("1") : F("0"));
  Serial.print(F(" local="));
  if (coinServoJob.active) {
    Serial.print(F("m"));
    Serial.print(coinServoJob.motor);
    Serial.print(':');
    Serial.print(coinServoJob.dispensedCount);
    Serial.print('/');
    Serial.print(coinServoJob.requestedCount);
  } else {
    Serial.print(F("idle"));
  }
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
  float amount = mapPulseCount(map, length, input.pulseCount);
  if (amount > 0.0f && input.name[0] == 'b' && BILL_AMOUNT_HALVE_AFTER_MAP) {
    amount = amount / 2.0f;
  }
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

// --- Bill pin: hardware-interrupt edge capture --------------------------
// The bill acceptor's pulse stream can be missed when loop() is busy with
// HTTP work or motor delay() calls. Capture every edge in an ISR and only
// run the final idle-gap commit from the main loop.
volatile uint8_t       coinIsrPulseCount     = 0;
volatile bool          coinIsrPending        = false;
volatile unsigned long coinIsrLastEdgeMs     = 0;
volatile unsigned long coinIsrPulseStartedMs = 0;
volatile bool          coinIsrPulseActive    = false;
volatile bool          coinIsrLastLevel      = true; // pulled-up idle = HIGH

volatile uint8_t       billIsrPulseCount     = 0;
volatile bool          billIsrPending        = false;
volatile unsigned long billIsrLastEdgeMs     = 0;
volatile unsigned long billIsrPulseStartedMs = 0;
volatile bool          billIsrPulseActive    = false;
volatile bool          billIsrLastLevel      = true; // pulled-up idle = HIGH

void IRAM_ATTR onCoinPinChange() {
  const bool level = (digitalRead(COIN_PIN) == HIGH);
  const unsigned long nowMs = millis();

  const bool startEdge = PULSE_ACTIVE_LOW
                            ? (coinIsrLastLevel && !level)
                            : (!coinIsrLastLevel && level);
  const bool endEdge   = PULSE_ACTIVE_LOW
                            ? (!coinIsrLastLevel && level)
                            : (coinIsrLastLevel && !level);

  if (startEdge) {
    coinIsrPulseActive = true;
    coinIsrPulseStartedMs = nowMs;
  }

  if (endEdge && coinIsrPulseActive) {
    const unsigned long widthMs = nowMs - coinIsrPulseStartedMs;
    coinIsrPulseActive = false;
    if (widthMs >= PULSE_MIN_WIDTH_MS &&
        widthMs <= PULSE_MAX_WIDTH_MS &&
        (nowMs - coinIsrLastEdgeMs) > PULSE_MIN_EDGE_GAP_MS) {
      ++coinIsrPulseCount;
      coinIsrPending = true;
      coinIsrLastEdgeMs = nowMs;
    }
  }

  coinIsrLastLevel = level;
}

void IRAM_ATTR onBillPinChange() {
  const bool level = (digitalRead(BILL_PIN) == HIGH);
  const unsigned long nowMs = millis();

  const bool startEdge = PULSE_ACTIVE_LOW
                            ? (billIsrLastLevel && !level)
                            : (!billIsrLastLevel && level);
  const bool endEdge   = PULSE_ACTIVE_LOW
                            ? (!billIsrLastLevel && level)
                            : (billIsrLastLevel && !level);

  if (startEdge) {
    billIsrPulseActive = true;
    billIsrPulseStartedMs = nowMs;
  }

  if (endEdge && billIsrPulseActive) {
    const unsigned long widthMs = nowMs - billIsrPulseStartedMs;
    billIsrPulseActive = false;
    if (widthMs >= PULSE_MIN_WIDTH_MS &&
        widthMs <= PULSE_MAX_WIDTH_MS &&
        (nowMs - billIsrLastEdgeMs) > PULSE_MIN_EDGE_GAP_MS) {
      ++billIsrPulseCount;
      billIsrPending = true;
      billIsrLastEdgeMs = nowMs;
    }
  }

  billIsrLastLevel = level;
}

void servicePulseInput(PulseInput& input, const PulseMap* map, size_t length) {
  // Coin input is fully ISR-driven; the loop only commits after idle gap.
  if (input.pin == COIN_PIN) {
    const unsigned long nowMs = millis();
    // Stuck-active recovery (matches polled path).
    if (coinIsrPulseActive &&
        (nowMs - coinIsrPulseStartedMs) > (PULSE_MAX_WIDTH_MS + 100)) {
      noInterrupts();
      coinIsrPulseActive = false;
      interrupts();
    }
    // Atomic snapshot.
    noInterrupts();
    const bool          pending  = coinIsrPending;
    const uint8_t       count    = coinIsrPulseCount;
    const unsigned long lastEdge = coinIsrLastEdgeMs;
    interrupts();

    if (pending && (nowMs - lastEdge) >= input.idleGapMs) {
      // Sync into the PulseInput struct so finalize prints/logs work.
      input.pulseCount = count;
      input.pending = true;
      input.lastEdgeMs = lastEdge;
      finalizePulseInput(input, map, length);
      // Reset ISR state to mirror the finalize.
      noInterrupts();
      coinIsrPending = false;
      coinIsrPulseCount = 0;
      interrupts();
    }
    return;
  }

  // Bill input is fully ISR-driven; the loop only commits after idle gap.
  if (input.pin == BILL_PIN) {
    const unsigned long nowMs = millis();
    // Stuck-active recovery (matches polled path).
    if (billIsrPulseActive &&
        (nowMs - billIsrPulseStartedMs) > (PULSE_MAX_WIDTH_MS + 100)) {
      noInterrupts();
      billIsrPulseActive = false;
      interrupts();
    }
    // Atomic snapshot.
    noInterrupts();
    const bool          pending  = billIsrPending;
    const uint8_t       count    = billIsrPulseCount;
    const unsigned long lastEdge = billIsrLastEdgeMs;
    interrupts();

    if (pending && (nowMs - lastEdge) >= input.idleGapMs) {
      // Sync into the PulseInput struct so finalize prints/logs work.
      input.pulseCount = count;
      input.pending = true;
      input.lastEdgeMs = lastEdge;
      finalizePulseInput(input, map, length);
      // Reset ISR state to mirror the finalize.
      noInterrupts();
      billIsrPending = false;
      billIsrPulseCount = 0;
      interrupts();
    }
    return;
  }

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

void postWithdrawCompleteIfAllDispenseIdle() {
  const bool allDispenseIdle =
    (taskCount == 0) &&
    !remoteTaskActive &&
    !coinServoJob.active &&
    !motor4Job.active &&
    !pendingWithdraw.active &&
    !withdrawJob.active;

  if (allDispenseIdle) {
    postWithdrawStatus("complete", 0, false);
    Serial.println(F("WITHDRAW status=complete (all dispense tasks done)"));
  }
}

void finishLocalMotor4() {
  const uint8_t dispensed = motor4Job.dispensedCount;
  m4IrMonitorActive = false;
  m4IrLatchedDetect = false;

  // After successful dispense, retract motor 4 backward for 10s.
  const unsigned long retractStartedMs = millis();
  unsigned long retractLastStepUs = micros();
  while (millis() - retractStartedMs < M4_BACKWARD_RUN_MS) {
    const unsigned long nowUs = micros();
    if (nowUs - retractLastStepUs >= M4_STEP_INTERVAL_US) {
      retractLastStepUs = nowUs;
      stepMotor4Backward();
    }
  }

  motor4Job = {};
  releaseMotor4();
  Serial.print(F("DONE motor=4 count="));
  Serial.println(dispensed);
  popQueueFront();
  postWithdrawCompleteIfAllDispenseIdle();
}

void failLocalMotor4(const __FlashStringHelper* reason) {
  m4IrMonitorActive = false;
  m4IrLatchedDetect = false;
  motor4Job = {};
  releaseMotor4();
  Serial.print(F("ERR motor=4 reason="));
  Serial.println(reason);
  popQueueFront();
}

void finishLocalCoinServoJob() {
  const uint8_t motor = coinServoJob.motor;
  const uint8_t dispensed = coinServoJob.dispensedCount;
  coinServoJob = {};
  if (motor >= 1 && motor <= DISPENSE_MOTOR_COUNT) {
    stopCoinServoMotor(motor - 1);
  }
  Serial.print(F("DONE motor="));
  Serial.print(motor);
  Serial.print(F(" count="));
  Serial.println(dispensed);
  popQueueFront();
  postWithdrawCompleteIfAllDispenseIdle();
}

void failLocalCoinServoJob(const __FlashStringHelper* reason) {
  const uint8_t motor = coinServoJob.motor;
  coinServoJob = {};
  if (motor >= 1 && motor <= DISPENSE_MOTOR_COUNT) {
    stopCoinServoMotor(motor - 1);
  }
  Serial.print(F("ERR motor="));
  Serial.print(motor);
  Serial.print(F(" reason="));
  Serial.println(reason);
  popQueueFront();
}

bool startLocalCoinServoJob(uint8_t motor, uint8_t count) {
  if (motor < 1 || motor > DISPENSE_MOTOR_COUNT || count == 0) {
    return false;
  }

  const uint8_t idx = motor - 1;
  if (!isCoinServoConfigured(idx)) {
    Serial.print(F("ERR motor="));
    Serial.print(motor);
    Serial.println(F(" reason=servo-not-configured"));
    return false;
  }

  attachCoinServo(idx);
  coinDispenseServos[idx].writeMicroseconds(coinServoConfig[idx].forwardUs);

  coinServoJob.active = true;
  coinServoJob.motor = motor;
  coinServoJob.requestedCount = count;
  coinServoJob.dispensedCount = 0;
  coinServoJob.sensorArmed = !isCoinServoIrDetected(idx);
  coinServoJob.startedMs = millis();
  coinServoJob.settleUntilMs = coinServoJob.startedMs + coinServoConfig[idx].startupSettleMs;

  Serial.print(F("OK DISPENSE motor="));
  Serial.print(motor);
  Serial.print(F(" count="));
  Serial.println(count);
  return true;
}

void serviceLocalCoinServoJob() {
  if (!coinServoJob.active) {
    return;
  }

  const uint8_t idx = coinServoJob.motor - 1;
  const CoinServoConfig& cfg = coinServoConfig[idx];
  const unsigned long nowMs = millis();

  if (nowMs - coinServoJob.startedMs >= cfg.timeoutMs) {
    failLocalCoinServoJob(F("timeout"));
    return;
  }

  if (nowMs < coinServoJob.settleUntilMs) {
    return;
  }

  const bool detected = isCoinServoIrDetected(idx);
  if (!coinServoJob.sensorArmed) {
    if (!detected) {
      coinServoJob.sensorArmed = true;
    }
    return;
  }

  if (!detected) {
    return;
  }

  ++coinServoJob.dispensedCount;
  coinServoJob.sensorArmed = false;
  Serial.print(F("EVENT motor="));
  Serial.print(coinServoJob.motor);
  Serial.print(F(" dispensed="));
  Serial.println(coinServoJob.dispensedCount);

  if (coinServoJob.dispensedCount >= coinServoJob.requestedCount) {
    finishLocalCoinServoJob();
  }
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

    // One full cycle completed when we transition BWD -> FWD.
    if (motor4Job.goingForward) {
      if (M4_ENABLE_CYCLE_FALLBACK && !motor4Job.sawDetectThisCycle) {
        ++motor4Job.dispensedCount;
        motor4Job.sensorArmed = false;
        motor4Job.detectStartedMs = 0;
        Serial.print(F("WARN M4 fallback cycle-count dispensed="));
        Serial.println(motor4Job.dispensedCount);
        if (motor4Job.dispensedCount >= motor4Job.requestedCount) {
          finishLocalMotor4();
          return;
        }
      }
      motor4Job.sawDetectThisCycle = false;
    }

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

  bool latchedDetect = false;
  noInterrupts();
  if (m4IrLatchedDetect) {
    latchedDetect = true;
    m4IrLatchedDetect = false;
  }
  interrupts();

  const bool detected = isM4IrDetected() || latchedDetect;
  if (!motor4Job.sensorArmed) {
    if (!detected) {
      motor4Job.sensorArmed = true;
      motor4Job.detectStartedMs = 0;
    }
    return;
  }

  if (detected) {
    // Stop as soon as IR detects a drop edge while armed.
    ++motor4Job.dispensedCount;
    motor4Job.sawDetectThisCycle = true;
    motor4Job.sensorArmed = false;
    motor4Job.detectStartedMs = 0;
    // Immediately retract so the pusher is ready for the next coin.
    motor4Job.goingForward = false;
    motor4Job.phaseStartedMs = nowMs;
    Serial.print(F("EVENT motor=4 dispensed="));
    Serial.println(motor4Job.dispensedCount);

    if (motor4Job.dispensedCount >= motor4Job.requestedCount) {
      finishLocalMotor4();
      return;
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

bool parseDurationMsToken(String token, unsigned long& durationMs) {
  token.trim();
  if (token.length() == 0) {
    return false;
  }

  String tokenUpper = token;
  tokenUpper.toUpperCase();

  unsigned long multiplier = 1;
  if (tokenUpper.endsWith(F("MS"))) {
    tokenUpper.remove(tokenUpper.length() - 2);
  } else if (tokenUpper.endsWith(F("SEC"))) {
    tokenUpper.remove(tokenUpper.length() - 3);
    multiplier = 1000;
  } else if (tokenUpper.endsWith(F("S"))) {
    tokenUpper.remove(tokenUpper.length() - 1);
    multiplier = 1000;
  }

  tokenUpper.trim();
  if (tokenUpper.length() == 0) {
    return false;
  }

  for (uint16_t i = 0; i < tokenUpper.length(); ++i) {
    const char ch = tokenUpper.charAt(i);
    if (ch < '0' || ch > '9') {
      return false;
    }
  }

  const unsigned long baseValue = static_cast<unsigned long>(tokenUpper.toInt());
  if (baseValue == 0) {
    return false;
  }

  durationMs = baseValue * multiplier;
  if (durationMs < SERVO_TEST_MIN_MS || durationMs > SERVO_TEST_MAX_MS) {
    return false;
  }

  return true;
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
    withdrawJob.stage = WD_IDLE;
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
  if (coinServoJob.active) return;
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
  setServoAngle(
    storageChannelForSlot(withdrawJob.currentSlot),
    storageCommandForSlot(withdrawJob.currentSlot, STORAGE_FORWARD_CMD)
  );
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
      if (nowMs - withdrawJob.stageStartedMs >= withdrawForwardMsForSlot(withdrawJob.currentSlot)) {
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
      if (nowMs - withdrawJob.stageStartedMs >= withdrawReverseMsForSlot(withdrawJob.currentSlot)) {
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
  // PC-bridge inbound: lines like "<ESP2<PONG" are treated as ESP2 replies.
  if (command.startsWith("<ESP2<")) {
    if (LEGACY_USB_BRIDGE_COMPAT) {
      handleEsp2Line(command.substring(6));
    } else {
      Serial.println(F("WARN usb-bridge relay disabled; ignoring <ESP2<... line"));
    }
    return;
  }
  String commandUpper = command;
  commandUpper.toUpperCase();

  if (commandUpper == F("STATUS")) {
    printStatus();
    sendEsp2Command(F("STATUS"));
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
  if (commandUpper == F("PINGESP2")) {
    sendEsp2Command(F("PING"));
    Serial.println(F("OK PINGESP2"));
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

  // SERVO_TEST <slot1to5|channel0to6> <FORWARD|BACKWARD> [duration]
  // duration examples: 10000, 10000MS, 10S, 10SEC
  // Convenience alias for quick spinner checks from Serial Monitor.

  if (commandUpper == F("SERVO_TEST") || commandUpper.startsWith(F("SERVO_TEST "))) {
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) {
      Serial.println(F("ERR format: SERVO_TEST <slot1to5|channel0to6> <FORWARD|BACKWARD> [duration]"));
      return;
    }

    const int thirdSpace = command.indexOf(' ', secondSpace + 1);

    String targetToken = command.substring(firstSpace + 1, secondSpace);
    String actionToken = (thirdSpace < 0)
      ? command.substring(secondSpace + 1)
      : command.substring(secondSpace + 1, thirdSpace);
    targetToken.trim();
    actionToken.trim();

    unsigned long testDurationMs = SERVO_TEST_DEFAULT_MS;
    if (thirdSpace >= 0) {
      String durationToken = command.substring(thirdSpace + 1);
      durationToken.trim();
      if (!parseDurationMsToken(durationToken, testDurationMs)) {
        Serial.print(F("ERR SERVO_TEST duration use "));
        Serial.print(SERVO_TEST_MIN_MS);
        Serial.print(F(".."));
        Serial.print(SERVO_TEST_MAX_MS);
        Serial.println(F(" ms (ex: 10000 or 10SEC)"));
        return;
      }
    }

    String actionUpper = actionToken;
    actionUpper.toUpperCase();
    int cmd = SERVO_STOP_CMD;
    bool validAction = true;
    if (actionUpper == F("FORWARD")) {
      cmd = SPINNER_RUN_CMD;
    } else if (actionUpper == F("BACKWARD")) {
      cmd = SPINNER_WITHDRAW_CMD;
    } else {
      validAction = false;
    }

    if (!validAction) {
      Serial.println(F("ERR SERVO_TEST action FORWARD|BACKWARD"));
      return;
    }

    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (SERVO_TEST)"));
    }

    const int target = targetToken.toInt();
    if (target >= 1 && target <= 5) {
      const uint8_t slotIndex = static_cast<uint8_t>(target - 1);
      spinStorageMotor(slotIndex, static_cast<uint8_t>(cmd));
      Serial.print(F("OK SERVO_TEST slot="));
      Serial.print(target);
      Serial.print(F(" action="));
      Serial.print(actionUpper);
      Serial.print(F(" durationMs="));
      Serial.println(testDurationMs);
      delay(testDurationMs);
      stopStorageMotor(slotIndex);
      Serial.print(F("OK SERVO_TEST slot="));
      Serial.print(target);
      Serial.println(F(" STOP"));
      return;
    }

    if (target >= 0 && target < SERVO_CHANNEL_COUNT) {
      setServoAngle(static_cast<uint8_t>(target), static_cast<uint8_t>(cmd));
      Serial.print(F("OK SERVO_TEST channel="));
      Serial.print(target);
      Serial.print(F(" action="));
      Serial.print(actionUpper);
      Serial.print(F(" durationMs="));
      Serial.println(testDurationMs);
      delay(testDurationMs);
      setServoAngle(static_cast<uint8_t>(target), SERVO_STOP_CMD);
      Serial.print(F("OK SERVO_TEST channel="));
      Serial.print(target);
      Serial.println(F(" STOP"));
      return;
    }

    Serial.println(F("ERR SERVO_TEST target slot1..5 or channel0..6"));
    return;
  }

  // ELEVATOR_TEST <FORWARD|BACKWARD> [duration]
  if (commandUpper.startsWith(F("ELEVATOR_TEST "))) {
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    String actionToken = (secondSpace < 0)
      ? command.substring(firstSpace + 1)
      : command.substring(firstSpace + 1, secondSpace);
    actionToken.trim();

    unsigned long testDurationMs = SERVO_TEST_DEFAULT_MS;
    if (secondSpace >= 0) {
      String durationToken = command.substring(secondSpace + 1);
      durationToken.trim();
      if (!parseDurationMsToken(durationToken, testDurationMs)) {
        Serial.print(F("ERR ELEVATOR_TEST duration use "));
        Serial.print(SERVO_TEST_MIN_MS);
        Serial.print(F(".."));
        Serial.print(SERVO_TEST_MAX_MS);
        Serial.println(F(" ms (ex: 10000 or 10SEC)"));
        return;
      }
    }

    String actionUpper = actionToken;
    actionUpper.toUpperCase();
    int cmd = SERVO_STOP_CMD;
    if (actionUpper == F("FORWARD")) {
      cmd = SPINNER_RUN_CMD;
    } else if (actionUpper == F("BACKWARD")) {
      cmd = SPINNER_WITHDRAW_CMD;
    } else {
      Serial.println(F("ERR ELEVATOR_TEST action FORWARD|BACKWARD"));
      return;
    }
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (ELEVATOR_TEST)"));
    }
    setServoAngle(ELEVATOR_LIFT_CHANNEL, static_cast<uint8_t>(cmd));
    Serial.print(F("OK ELEVATOR_TEST action="));
    Serial.print(actionUpper);
    Serial.print(F(" durationMs="));
    Serial.println(testDurationMs);
    delay(testDurationMs);
    setServoAngle(ELEVATOR_LIFT_CHANNEL, SERVO_STOP_CMD);
    Serial.println(F("OK ELEVATOR_TEST STOP"));
    return;
  }

  // OUTPUT_TEST <FORWARD|BACKWARD> [duration]
  if (commandUpper.startsWith(F("OUTPUT_TEST "))) {
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    String actionToken = (secondSpace < 0)
      ? command.substring(firstSpace + 1)
      : command.substring(firstSpace + 1, secondSpace);
    actionToken.trim();

    unsigned long testDurationMs = SERVO_TEST_DEFAULT_MS;
    if (secondSpace >= 0) {
      String durationToken = command.substring(secondSpace + 1);
      durationToken.trim();
      if (!parseDurationMsToken(durationToken, testDurationMs)) {
        Serial.print(F("ERR OUTPUT_TEST duration use "));
        Serial.print(SERVO_TEST_MIN_MS);
        Serial.print(F(".."));
        Serial.print(SERVO_TEST_MAX_MS);
        Serial.println(F(" ms (ex: 10000 or 10SEC)"));
        return;
      }
    }

    String actionUpper = actionToken;
    actionUpper.toUpperCase();
    int cmd = SERVO_STOP_CMD;
    if (actionUpper == F("FORWARD")) {
      cmd = SPINNER_RUN_CMD;
    } else if (actionUpper == F("BACKWARD")) {
      cmd = SPINNER_WITHDRAW_CMD;
    } else {
      Serial.println(F("ERR OUTPUT_TEST action FORWARD|BACKWARD"));
      return;
    }
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (OUTPUT_TEST)"));
    }
    setServoAngle(ELEVATOR_OUT_CHANNEL, static_cast<uint8_t>(cmd));
    Serial.print(F("OK OUTPUT_TEST action="));
    Serial.print(actionUpper);
    Serial.print(F(" durationMs="));
    Serial.println(testDurationMs);
    delay(testDurationMs);
    setServoAngle(ELEVATOR_OUT_CHANNEL, SERVO_STOP_CMD);
    Serial.println(F("OK OUTPUT_TEST STOP"));
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

  if (commandUpper.startsWith(F("FLOORTEST "))) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    if (!motionArmed) {
      motionArmed = true;
      pcaAllChannelsOff();
      Serial.println(F("OK ARMED (FLOORTEST)"));
    }
    if (billRoute.active) {
      Serial.println(F("ERR lift busy (bill route active)"));
      return;
    }

    String arg = command.substring(10);
    arg.trim();
    String argUpper = arg;
    argUpper.toUpperCase();

    if (argUpper == F("ALL")) {
      for (uint8_t slot = 0; slot < 5; ++slot) {
        if (!moveLiftDirectToSlotInstantStop(slot)) {
          Serial.print(F("ERR FLOORTEST ALL failed at IR5-"));
          Serial.println(slot + 1);
          return;
        }
        delay(250);
      }
      Serial.println(F("OK FLOORTEST ALL"));
      return;
    }

    const int slot = arg.toInt();
    if (slot < 1 || slot > 5) {
      Serial.println(F("ERR FLOORTEST slot 1..5 or ALL"));
      return;
    }

    moveLiftDirectToSlotInstantStop(static_cast<uint8_t>(slot - 1));
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

  if (commandUpper.startsWith(F("MOTOR ")) || commandUpper.startsWith(F("SERVO "))) {
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
    Serial.print(F("OK MOTOR channel="));
    Serial.print(channel);
    Serial.print(F(" cmd="));
    Serial.println(angle);
    return;
  }

  if (commandUpper.startsWith(F("PWMUS "))) {
    if (!SERVOS_ENABLED) {
      Serial.println(F("ERR servos-disabled"));
      return;
    }
    if (!motionArmed) {
      Serial.println(F("ERR motion-disarmed"));
      return;
    }
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("ERR format"));
      return;
    }
    const int channelValue = command.substring(firstSpace + 1, secondSpace).toInt();
    const int pulseUs = command.substring(secondSpace + 1).toInt();
    if (channelValue < 0 || channelValue >= SERVO_CHANNEL_COUNT) {
      Serial.println(F("ERR channel 0..6"));
      return;
    }
    if (pulseUs < 500 || pulseUs > 2500) {
      Serial.println(F("ERR us 500..2500"));
      return;
    }
    pcaSetPwm(static_cast<uint8_t>(channelValue), 0, pcaOffCountFromPulseUs(pulseUs));
    Serial.print(F("OK PWMUS channel="));
    Serial.print(channelValue);
    Serial.print(F(" us="));
    Serial.println(pulseUs);
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
  // silently so stray queued DEPOSIT messages don't trigger ERR loops
  // (which previously crashed the panic handler and rebooted the device).
  if (commandUpper.startsWith(F("DEPOSIT"))) {
    Serial.println(F("OK DEPOSIT (no-op; insert bill into acceptor)"));
    return;
  }

  // PCATEST sweeps every PCA9685 channel (0..6) one at a time so a human can
  // visually confirm whether each servo physically moves. Use this to diagnose
  // dead V+ rail / loose connectors / bad servos. Output is verbose on Serial.
  if (commandUpper.equals(F("PCATEST"))) {
    Serial.println(F("PCATEST START (7 channels)"));
    for (uint8_t ch = 0; ch < SERVO_CHANNEL_COUNT; ch++) {
      Serial.print(F("PCATEST CH=")); Serial.print(ch);
      Serial.println(F(" -> 0deg"));
      setServoAngle(ch, 0);
      delay(800);
      Serial.print(F("PCATEST CH=")); Serial.print(ch);
      Serial.println(F(" -> 180deg"));
      setServoAngle(ch, 180);
      delay(800);
      Serial.print(F("PCATEST CH=")); Serial.print(ch);
      Serial.println(F(" -> 90deg (neutral)"));
      setServoAngle(ch, 90);
      delay(500);
    }
    Serial.println(F("PCATEST DONE (if nothing moved -> check V+ supply / common GND)"));
    return;
  }

  // I2CDIAG reads the idle voltage state of SDA and SCL with internal pullups
  // enabled. This proves whether the bus is electrically alive *before* any
  // I2C traffic. Expected idle: BOTH HIGH. If either reads LOW, that line is
  // shorted to GND or held by a dead device. If both read HIGH but I2CSCAN
  // still finds nothing -> the chip is unpowered (VCC=0V) or fried.
  if (commandUpper.equals(F("I2CDIAG"))) {
    if (!USE_PCA9685) {
      Serial.println(F("I2CDIAG skipped (direct-servo mode)"));
      return;
    }
    Wire.end();
    delay(20);
    const uint8_t SDA_PIN = 21;
    const uint8_t SCL_PIN = 22;
    // Float test (no pullups) - tells us if anything is actively driving the line
    pinMode(SDA_PIN, INPUT);
    pinMode(SCL_PIN, INPUT);
    delay(5);
    int sdaFloat = digitalRead(SDA_PIN);
    int sclFloat = digitalRead(SCL_PIN);
    // Pulled-up test - tells us if any wire is shorted to GND
    pinMode(SDA_PIN, INPUT_PULLUP);
    pinMode(SCL_PIN, INPUT_PULLUP);
    delay(5);
    int sdaPU = digitalRead(SDA_PIN);
    int sclPU = digitalRead(SCL_PIN);
    Serial.print(F("I2CDIAG float SDA=")); Serial.print(sdaFloat ? F("HIGH") : F("LOW"));
    Serial.print(F(" SCL="));              Serial.println(sclFloat ? F("HIGH") : F("LOW"));
    Serial.print(F("I2CDIAG pullup SDA=")); Serial.print(sdaPU ? F("HIGH") : F("LOW"));
    Serial.print(F(" SCL="));              Serial.println(sclPU ? F("HIGH") : F("LOW"));
    if (sdaPU == LOW && sclPU == LOW) {
      Serial.println(F("I2CDIAG -> BOTH lines stuck LOW: SDA and SCL likely shorted to GND or PCA9685 fried holding bus low"));
    } else if (sdaPU == LOW) {
      Serial.println(F("I2CDIAG -> SDA stuck LOW: SDA wire shorted to GND or device holding it low"));
    } else if (sclPU == LOW) {
      Serial.println(F("I2CDIAG -> SCL stuck LOW: SCL wire shorted to GND or device holding it low"));
    } else {
      Serial.println(F("I2CDIAG -> bus electrically OK (both idle HIGH); if I2CSCAN still finds nothing, PCA9685 has no VCC or is dead"));
    }
    // Restore default I2C
    Wire.end();
    delay(20);
    Wire.begin(21, 22);
    return;
  }

  // I2CSCAN walks every I2C address 0x03..0x77 and reports which devices ACK.
  // Use this to confirm the ESP32 can actually reach the PCA9685 (expected at 0x40).
  // If 0x40 is missing -> SDA/SCL wiring, missing pullups, or dead PCA9685.
  if (commandUpper.equals(F("I2CSCAN"))) {
    if (!USE_PCA9685) {
      Serial.println(F("I2CSCAN skipped (direct-servo mode)"));
      return;
    }
    auto runScan = [](const __FlashStringHelper* label, uint8_t sda, uint8_t scl, bool pullups) -> uint8_t {
      Serial.print(F("I2CSCAN ")); Serial.print(label);
      Serial.print(F(" SDA=")); Serial.print(sda);
      Serial.print(F(" SCL=")); Serial.print(scl);
      Serial.print(F(" pullups=")); Serial.println(pullups ? F("INTERNAL") : F("OFF"));
      Wire.end();
      delay(50);
      if (pullups) {
        pinMode(sda, INPUT_PULLUP);
        pinMode(scl, INPUT_PULLUP);
      }
      Wire.begin(sda, scl);
      Wire.setClock(100000);
      delay(50);
      uint8_t found = 0;
      for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
          Serial.print(F("  FOUND 0x"));
          if (addr < 16) Serial.print(F("0"));
          Serial.println(addr, HEX);
          found++;
        }
      }
      Serial.print(F("  total=")); Serial.println(found);
      return found;
    };
    uint8_t a = runScan(F("[A normal]"),   21, 22, false);
    uint8_t b = runScan(F("[B pullups]"),  21, 22, true);
    uint8_t c = runScan(F("[C swapped]"),  22, 21, true);
    Serial.print(F("I2CSCAN SUMMARY normal=")); Serial.print(a);
    Serial.print(F(" pullups=")); Serial.print(b);
    Serial.print(F(" swapped=")); Serial.println(c);
    if (a + b + c == 0) {
      Serial.println(F("I2CSCAN ALL FAILED -> wires loose/broken or PCA9685 dead"));
    } else if (c > 0 && a == 0 && b == 0) {
      Serial.println(F("I2CSCAN -> SDA/SCL ARE SWAPPED in your wiring"));
    } else if (b > 0 && a == 0) {
      Serial.println(F("I2CSCAN -> board missing pullups; add 4.7k ohm SDA->VCC and SCL->VCC"));
    }
    // Restore default
    Wire.end();
    delay(20);
    Wire.begin(21, 22);
    return;
  }

  if (commandUpper.startsWith(F("PCAREGS"))) {
    if (!USE_PCA9685) {
      Serial.println(F("PCAREGS skipped (direct-servo mode)"));
      return;
    }
    if (!pcaIsPresent()) {
      Serial.println(F("ERR pca-offline"));
      return;
    }

    const uint8_t mode1 = pcaReadRegister(PCA9685_MODE1);
    const uint8_t mode2 = pcaReadRegister(PCA9685_MODE2);
    const uint8_t prescale = pcaReadRegister(PCA9685_PRESCALE);

    Serial.print(F("PCA MODE1=0x"));
    if (mode1 < 16) Serial.print('0');
    Serial.print(mode1, HEX);
    Serial.print(F(" MODE2=0x"));
    if (mode2 < 16) Serial.print('0');
    Serial.print(mode2, HEX);
    Serial.print(F(" PRE=0x"));
    if (prescale < 16) Serial.print('0');
    Serial.println(prescale, HEX);

    int channel = -1;
    const int firstSpace = command.indexOf(' ');
    if (firstSpace > 0) {
      channel = command.substring(firstSpace + 1).toInt();
    }
    if (channel < 0 || channel >= SERVO_CHANNEL_COUNT) {
      channel = 0;
    }

    const uint8_t base = PCA9685_LED0_ON_L + static_cast<uint8_t>(4 * channel);
    const uint16_t onCount =
      static_cast<uint16_t>(pcaReadRegister(base + 0)) |
      (static_cast<uint16_t>(pcaReadRegister(base + 1) & 0x0F) << 8);
    const uint16_t offCount =
      static_cast<uint16_t>(pcaReadRegister(base + 2)) |
      (static_cast<uint16_t>(pcaReadRegister(base + 3) & 0x0F) << 8);

    Serial.print(F("PCA CH="));
    Serial.print(channel);
    Serial.print(F(" ON="));
    Serial.print(onCount);
    Serial.print(F(" OFF="));
    Serial.println(offCount);
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

void handleEsp2Line(const String& line) {
  if (line.length() == 0) {
    return;
  }

  esp2Online = true;

  if (line.startsWith("OK DISPENSE")) {
    Serial.print(F("ESP2 ACK: "));
    Serial.println(line);
    return;
  }

  if (line.startsWith("DONE")) {
    int motor = 0;
    int count = 0;
    float amount = 0.0f;
    const int motorStart = line.indexOf("motor=");
    const int countStart = line.indexOf("count=");
    const int amountStart = line.indexOf("amount=");

    if (motorStart >= 0) {
      motor = line.substring(motorStart + 6).toInt();
    }
    if (countStart >= 0) {
      count = line.substring(countStart + 6).toInt();
    }
    if (amountStart >= 0) {
      amount = line.substring(amountStart + 7).toFloat();
    }

    Serial.print(F("Received DONE message: motor="));
    Serial.print(motor);
    Serial.print(F(", count="));
    Serial.print(count);
    Serial.print(F(", amount="));
    Serial.println(amount);

    // Mark the current ESP2 task as completed so watchdog won't timeout.
    if (remoteTaskActive) {
      remoteTaskActive = false;
      remoteTaskSentMs = 0;
      if (taskCount > 0 && taskQueue[0].motor == remoteTaskMotor) {
        popQueueFront();
      }
    }

    postWithdrawCompleteIfAllDispenseIdle();
    return;
  }

  if (line.startsWith("ERR")) {
    Serial.print(F("ESP2 ERR: "));
    Serial.println(line);
    if (remoteTaskActive) {
      remoteTaskActive = false;
      remoteTaskSentMs = 0;
      if (taskCount > 0 && taskQueue[0].motor == remoteTaskMotor) {
        popQueueFront();
      }
    }
    return;
  } else {
    Serial.print(F("Unknown message from ESP2: "));
    Serial.println(line);
  }
}

void readEsp2Lines() {
  while (RaspiSerial.available() > 0) {
    const char incoming = static_cast<char>(RaspiSerial.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      handleEsp2Line(esp2LineBuffer);
      esp2LineBuffer = "";
      continue;
    }

    // Keep only readable ASCII to prevent gibberish logs from UART noise.
    if (incoming < 32 || incoming > 126) {
      continue;
    }

    if (esp2LineBuffer.length() < 128) {
      esp2LineBuffer += incoming;
    }
  }
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

}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  RaspiSerial.begin(RASPI_UART_BAUD, SERIAL_8N1, RASPI_UART_RX_PIN, RASPI_UART_TX_PIN);
  connectWifi();

  pinMode(M4_IN1_PIN, OUTPUT);
  pinMode(M4_IN2_PIN, OUTPUT);
  pinMode(M4_IN3_PIN, OUTPUT);
  pinMode(M4_IN4_PIN, OUTPUT);
  pinMode(M4_IR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(M4_IR_PIN), onM4IrPinChange, CHANGE);
  pinMode(BILL_PIN, INPUT_PULLUP);
  pinMode(COIN_PIN, INPUT_PULLUP);
  if (LOCAL_COIN_SERVO_MODE) {
    for (uint8_t index = 0; index < DISPENSE_MOTOR_COUNT; ++index) {
      if (coinServoConfig[index].irPin != INVALID_GPIO_PIN) {
        pinMode(coinServoConfig[index].irPin, INPUT_PULLUP);
      }
    }
  }
  // Bill pin uses a hardware interrupt so pulses are not missed when loop()
  // is busy with HTTP, motor delays, etc.
  coinIsrLastLevel = (digitalRead(COIN_PIN) == HIGH);
  attachInterrupt(digitalPinToInterrupt(COIN_PIN), onCoinPinChange, CHANGE);
  billIsrLastLevel = (digitalRead(BILL_PIN) == HIGH);
  attachInterrupt(digitalPinToInterrupt(BILL_PIN), onBillPinChange, CHANGE);
  if (IR5_SENSORS_ENABLED) {
    for (uint8_t index = 0; index < 5; ++index) {
      const uint8_t irPin = IR5_PINS[index];
      // GPIO34/35 are input-only and do not support internal pull-ups.
      if (irPin == 34 || irPin == 35 || irPin == 36 || irPin == 39) {
        pinMode(irPin, INPUT);
      } else {
        pinMode(irPin, INPUT_PULLUP);
      }
      const bool initial = isIr5RawDetected(index);
      ir5RawLastState[index] = initial;
      ir5StableState[index] = initial;
      ir5RawChangedMs[index] = millis();
    }
  } else {
    // IR5 sensors disabled: IR5 GPIOs (13/25/26/33) are now servo outputs.
    // Mark all IR5 states as not-detected so any residual logic stays inert.
    for (uint8_t index = 0; index < 5; ++index) {
      ir5RawLastState[index] = false;
      ir5StableState[index] = false;
      ir5RawChangedMs[index] = millis();
    }
  }

  releaseMotor4();

  if (USE_PCA9685) {
    // PCA9685 on I2C (SDA=21, SCL=22).
    Wire.begin(21, 22);
  }
  const bool pcaOnlineAtBoot = pcaIsPresent();
  if (pcaOnlineAtBoot) {
    if (USE_PCA9685) {
      Serial.println(F("PCA9685 detected at 0x40"));
    } else {
      Serial.println(F("DIRECT SERVO MODE (ESP32 GPIO PWM)"));
    }
    pcaInit50Hz();
    if (SERVOS_ENABLED && motionArmed) {
      homeServos();
    } else {
      pcaAllChannelsOff();
    }
  } else {
    Serial.println(F("PCA9685 NOT FOUND on I2C (SDA=21 SCL=22) -> check VCC/GND/SDA/SCL wiring; servos disabled this boot"));
  }

  coinInput.lastLevel = (digitalRead(COIN_PIN) == HIGH);
  billInput.lastLevel = (digitalRead(BILL_PIN) == HIGH);
  lastStatusPrintMs = millis();

  Serial.println(F("ESP1 - CASH ATM CONTROLLER READY"));
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

  if (AUTO_LIFT_TO_IR5_1_ON_BOOT && SERVOS_ENABLED && !billRoute.active && pcaOnlineAtBoot) {
    motionArmed = true;
    pcaAllChannelsOff();
    startLiftToIr51Sequence(F("BOOT"));
    Serial.println(F("AUTO: LIFTTO1 requested"));
  } else if (AUTO_LIFT_TO_IR5_1_ON_BOOT && SERVOS_ENABLED && !pcaOnlineAtBoot) {
    Serial.println(F("AUTO: LIFTTO1 skipped (PCA9685 offline)"));
  }

  if (AUTO_LIFT_TO_BOTTOM_ON_BOOT && SERVOS_ENABLED && !billRoute.active && pcaOnlineAtBoot) {
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

  Serial.println(F("INFO: local-control mode"));

  sendEsp2Command(F("PING"));
}

void loop() {
  readUsbCommands();
  readEsp2Lines();
  // Pulse inputs run FIRST so short coin/bill pulses are never starved by
  // the blocking HTTP call inside serviceCloudCommandPoll().
  serviceInputDiag();

  if (ACCEPTORS_CONNECTED && COIN_INPUT_ENABLED) {
    servicePulseInput(coinInput, COIN_MAP, sizeof(COIN_MAP) / sizeof(COIN_MAP[0]));
  }
  if (ACCEPTORS_CONNECTED && BILL_INPUT_ENABLED) {
    servicePulseInput(billInput, BILL_MAP, sizeof(BILL_MAP) / sizeof(BILL_MAP[0]));
  }
  // Cloud poll runs AFTER pulse detection so HTTP blocking doesn't miss pulses.
  serviceCloudCommandPoll();
  serviceIr5Sensors();
  if (ACCEPTORS_CONNECTED && AUTO_BILL_ROUTE_ENABLED) {
    startBillRouteIfPending();
    serviceBillRoute();
  }
  serviceLiftToIr51();
  serviceElevatorPark();
  serviceLiftHardLimits();
  serviceLocalCoinServoJob();
  serviceLocalMotor4();
  // ESP2 watchdog: if a DISPENSE was sent and no DONE/ERR came back within
  // DISPENSE_TIMEOUT_MS, log + clear so subsequent tasks can be sent.
    if (!LOCAL_COIN_SERVO_MODE && DISPENSE_TIMEOUT_MS > 0 &&
      remoteTaskActive && remoteTaskSentMs > 0 &&
      millis() - remoteTaskSentMs > DISPENSE_TIMEOUT_MS) {
    Serial.print(F("WARN esp2 timeout motor="));
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

  const unsigned long nowMs = millis();
  if (LOOP_HEARTBEAT_VERBOSE && (nowMs - lastLoopHeartbeatMs >= LOOP_HEARTBEAT_MS)) {
    lastLoopHeartbeatMs = nowMs;
    Serial.print(F("ALIVE net=off"));
    Serial.print(F(" q="));
    Serial.print(taskCount);
    Serial.print(F(" withdraw="));
    Serial.print(withdrawJob.active ? F("1") : F("0"));
    Serial.println();
  }
  if (STATUS_VERBOSE_LOG && (nowMs - lastStatusPrintMs >= STATUS_PRINT_MS)) {
    lastStatusPrintMs = nowMs;
    printStatus();
  }
}