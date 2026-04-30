#include <Wire.h>

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t PCA9685_ADDRESS = 0x40;
constexpr uint8_t PCA9685_MODE1 = 0x00;
constexpr uint8_t PCA9685_PRESCALE = 0xFE;
constexpr uint8_t PCA9685_LED0_ON_L = 0x06;

constexpr uint8_t LIFT_CHANNEL = 5;   // PCA CH5
constexpr uint16_t SERVO_PWM_MIN = 110;
constexpr uint16_t SERVO_PWM_MAX = 510;

constexpr uint8_t LIFT_BOTTOM_ANGLE = 45;
constexpr uint8_t LIFT_MID_ANGLE = 90;
constexpr uint8_t LIFT_TOP_ANGLE = 145;

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
  pcaWriteRegister(PCA9685_PRESCALE, 121); // 50 Hz for servos
  pcaWriteRegister(PCA9685_MODE1, 0x20);
  delay(5);
  pcaWriteRegister(PCA9685_MODE1, pcaReadRegister(PCA9685_MODE1) | 0xA1);
}

void setServoAngle(uint8_t channel, uint8_t angle) {
  if (angle > 180) {
    angle = 180;
  }
  const uint16_t pulse = SERVO_PWM_MIN + ((SERVO_PWM_MAX - SERVO_PWM_MIN) * angle) / 180;
  pcaSetPwm(channel, 0, pulse);
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);
  Wire.begin(SDA_PIN, SCL_PIN);
  pcaInit50Hz();

  Serial.println("PCA CH5 lift test ready");
  Serial.println("Commands: BOTTOM, MID, TOP, SWEEP");
  setServoAngle(LIFT_CHANNEL, LIFT_MID_ANGLE);
}

void loop() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "BOTTOM") {
    setServoAngle(LIFT_CHANNEL, LIFT_BOTTOM_ANGLE);
    Serial.println("OK BOTTOM");
  } else if (cmd == "MID") {
    setServoAngle(LIFT_CHANNEL, LIFT_MID_ANGLE);
    Serial.println("OK MID");
  } else if (cmd == "TOP") {
    setServoAngle(LIFT_CHANNEL, LIFT_TOP_ANGLE);
    Serial.println("OK TOP");
  } else if (cmd == "SWEEP") {
    Serial.println("SWEEP start");
    setServoAngle(LIFT_CHANNEL, LIFT_BOTTOM_ANGLE);
    delay(800);
    setServoAngle(LIFT_CHANNEL, LIFT_TOP_ANGLE);
    delay(1000);
    setServoAngle(LIFT_CHANNEL, LIFT_MID_ANGLE);
    delay(700);
    Serial.println("SWEEP done");
  } else {
    Serial.println("ERR command");
  }
}
