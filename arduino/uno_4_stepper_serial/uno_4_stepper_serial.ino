#include <AccelStepper.h>

// 4x 28BYJ-48 + ULN2003 on Arduino Uno
// Serial command format:
//   M<n> <steps>
// Examples:
//   M1 2048    -> move motor 1 forward about 1 revolution
//   M2 -1024   -> move motor 2 backward about half revolution
//   STOP       -> stop all motors immediately
//   ZERO       -> set current positions to 0
//   STATUS     -> print current target and position of all motors

namespace {

constexpr long SERIAL_BAUD = 115200;
constexpr float MAX_SPEED = 700.0;
constexpr float ACCELERATION = 450.0;

AccelStepper motor1(AccelStepper::HALF4WIRE, 2, 4, 3, 5);
AccelStepper motor2(AccelStepper::HALF4WIRE, 6, 8, 7, 9);
AccelStepper motor3(AccelStepper::HALF4WIRE, 10, 12, 11, 13);
AccelStepper motor4(AccelStepper::HALF4WIRE, A0, A2, A1, A3);

AccelStepper* const motors[] = { &motor1, &motor2, &motor3, &motor4 };
constexpr size_t MOTOR_COUNT = sizeof(motors) / sizeof(motors[0]);

String commandBuffer;

void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  M1 2048   -> move motor 1 forward"));
  Serial.println(F("  M2 -2048  -> move motor 2 backward"));
  Serial.println(F("  STOP      -> stop all motors"));
  Serial.println(F("  ZERO      -> set all current positions to zero"));
  Serial.println(F("  STATUS    -> show motor positions"));
  Serial.println(F("  HELP      -> show this message"));
}

void printStatus() {
  for (size_t index = 0; index < MOTOR_COUNT; ++index) {
    Serial.print(F("M"));
    Serial.print(index + 1);
    Serial.print(F(" pos="));
    Serial.print(motors[index]->currentPosition());
    Serial.print(F(" target="));
    Serial.println(motors[index]->targetPosition());
  }
}

void stopAllMotors() {
  for (size_t index = 0; index < MOTOR_COUNT; ++index) {
    motors[index]->stop();
  }
  Serial.println(F("OK STOP"));
}

void zeroAllPositions() {
  for (size_t index = 0; index < MOTOR_COUNT; ++index) {
    motors[index]->setCurrentPosition(0);
  }
  Serial.println(F("OK ZERO"));
}

void moveMotorByCommand(int motorNumber, long relativeSteps) {
  if (motorNumber < 1 || motorNumber > static_cast<int>(MOTOR_COUNT)) {
    Serial.println(F("ERR invalid motor"));
    return;
  }

  AccelStepper* motor = motors[motorNumber - 1];
  long nextTarget = motor->currentPosition() + relativeSteps;
  motor->moveTo(nextTarget);

  Serial.print(F("OK M"));
  Serial.print(motorNumber);
  Serial.print(F(" target="));
  Serial.println(nextTarget);
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) {
    return;
  }

  command.toUpperCase();

  if (command == F("HELP")) {
    printHelp();
    return;
  }

  if (command == F("STATUS")) {
    printStatus();
    return;
  }

  if (command == F("STOP")) {
    stopAllMotors();
    return;
  }

  if (command == F("ZERO")) {
    zeroAllPositions();
    return;
  }

  if (command.charAt(0) != 'M') {
    Serial.println(F("ERR unknown command"));
    return;
  }

  int spaceIndex = command.indexOf(' ');
  if (spaceIndex < 0) {
    Serial.println(F("ERR format use: M<n> <steps>"));
    return;
  }

  int motorNumber = command.substring(1, spaceIndex).toInt();
  long steps = command.substring(spaceIndex + 1).toInt();
  moveMotorByCommand(motorNumber, steps);
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      handleCommand(commandBuffer);
      commandBuffer = "";
      continue;
    }

    if (commandBuffer.length() < 48) {
      commandBuffer += incoming;
    }
  }
}

void configureMotor(AccelStepper& motor) {
  motor.setMaxSpeed(MAX_SPEED);
  motor.setAcceleration(ACCELERATION);
}

}

void setup() {
  Serial.begin(SERIAL_BAUD);

  configureMotor(motor1);
  configureMotor(motor2);
  configureMotor(motor3);
  configureMotor(motor4);

  Serial.println(F("UNO 4-stepper serial controller ready"));
  printHelp();
}

void loop() {
  readSerialCommands();

  for (size_t index = 0; index < MOTOR_COUNT; ++index) {
    motors[index]->run();
  }
}