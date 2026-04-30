#include <Wire.h>

namespace {

constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t PCA9685_ADDRESS = 0x40;
constexpr uint8_t PCA9685_MODE1 = 0x00;
constexpr uint8_t PCA9685_PRESCALE = 0xFE;
constexpr uint8_t PCA9685_LED0_ON_L = 0x06;

constexpr uint8_t TEST_CHANNEL = 0; // Change to 1..6 to test other channels.
constexpr uint16_t SERVO_PWM_MIN = 110;
constexpr uint16_t SERVO_PWM_MAX = 510;

// 360 servo control commands:
// 90 is nominal stop, >90 rotates one direction, <90 rotates the other direction.
constexpr uint8_t SERVO_STOP_CMD = 90;
constexpr uint8_t SERVO_FORWARD_CMD = 120;
constexpr uint8_t SERVO_REVERSE_CMD = 60;

constexpr uint16_t STOP_HOLD_MS = 1000;
constexpr uint16_t RUN_HOLD_MS = 2500;

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
  pcaWriteRegister(PCA9685_PRESCALE, 121); // 50 Hz
  pcaWriteRegister(PCA9685_MODE1, 0x20);
  delay(5);
  pcaWriteRegister(PCA9685_MODE1, pcaReadRegister(PCA9685_MODE1) | 0xA1);
}

void setServoCommand(uint8_t channel, uint8_t command) {
  if (channel >= 16) {
    return;
  }
  if (command > 180) {
    command = 180;
  }

  const uint16_t pulse = SERVO_PWM_MIN + ((SERVO_PWM_MAX - SERVO_PWM_MIN) * command) / 180;
  pcaSetPwm(channel, 0, pulse);
}

}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);
  pcaInit50Hz();
  setServoCommand(TEST_CHANNEL, SERVO_STOP_CMD);
}

void loop() {
  setServoCommand(TEST_CHANNEL, SERVO_STOP_CMD);
  delay(STOP_HOLD_MS);

  setServoCommand(TEST_CHANNEL, SERVO_FORWARD_CMD);
  delay(RUN_HOLD_MS);

  setServoCommand(TEST_CHANNEL, SERVO_STOP_CMD);
  delay(STOP_HOLD_MS);

  setServoCommand(TEST_CHANNEL, SERVO_REVERSE_CMD);
  delay(RUN_HOLD_MS);
}
