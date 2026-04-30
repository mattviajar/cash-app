/*
 * Dual DC Motor Reversing via 4-Channel Relay Module
 * 
 * 2 motors controlled by 4 relays (2 relays per motor).
 * Hold BTN_FWD = both motors spin forward.
 * Hold BTN_REV = both motors spin in reverse.
 * Release = both motors brake-stop.
 *
 * Wiring:
 *   Relay module IN = active-LOW (LOW turns relay ON)
 *
 *   Motor 1: R1 (GPIO25) + R2 (GPIO26)
 *     R1 COM -> Motor1 wire A | R1 NO -> +12V | R1 NC -> GND
 *     R2 COM -> Motor1 wire B | R2 NO -> GND   | R2 NC -> +12V
 *
 *   Motor 2: R3 (GPIO27) + R4 (GPIO14)
 *     R3 COM -> Motor2 wire A | R3 NO -> +12V | R3 NC -> GND
 *     R4 COM -> Motor2 wire B | R4 NO -> GND   | R4 NC -> +12V
 *
 *   BTN_FWD = GPIO32 (hold = forward)
 *   BTN_REV = GPIO33 (hold = reverse)
 *   Both buttons: one side to GPIO, other side to GND (uses INPUT_PULLUP)
 *
 *   Power: 5V supply -> relay VCC/JD-VCC and relay GND
 *          ESP32 GND -> relay GND (common ground)
 *          12V supply -> relay NO/NC terminals (motor power side only)
 */

// ── Relay pins (active-LOW) ───────────────────────────────────
#define R1  25   // Motor 1 pole A
#define R2  26   // Motor 1 pole B
#define R3  27   // Motor 2 pole A
#define R4  14   // Motor 2 pole B

// ── Button pins ───────────────────────────────────────────────
#define BTN_FWD  32
#define BTN_REV  33

// ── Dead-time before reversing direction (ms) ─────────────────
#define DEAD_TIME_MS  200

// ── Relay helpers (active-LOW board) ─────────────────────────
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

typedef enum { STATE_STOP, STATE_FWD, STATE_REV } MotorState;
MotorState currentState = STATE_STOP;

void motorsStop() {
  // Brake: both poles same potential (all NC = all relays OFF)
  digitalWrite(R1, RELAY_OFF);
  digitalWrite(R2, RELAY_OFF);
  digitalWrite(R3, RELAY_OFF);
  digitalWrite(R4, RELAY_OFF);
}

void motorsForward() {
  // R1 ON = Motor1 A -> +12V via NO
  // R2 ON = Motor1 B -> GND  via NO
  // R3 ON = Motor2 A -> +12V via NO
  // R4 ON = Motor2 B -> GND  via NO
  digitalWrite(R1, RELAY_ON);
  digitalWrite(R2, RELAY_ON);
  digitalWrite(R3, RELAY_ON);
  digitalWrite(R4, RELAY_ON);
}

void motorsReverse() {
  // R1 OFF = Motor1 A -> GND  via NC
  // R2 OFF = Motor1 B -> +12V via NC
  // R3 OFF = Motor2 A -> GND  via NC
  // R4 OFF = Motor2 B -> +12V via NC
  digitalWrite(R1, RELAY_OFF);
  digitalWrite(R2, RELAY_OFF);
  digitalWrite(R3, RELAY_OFF);
  digitalWrite(R4, RELAY_OFF);
}

void setup() {
  pinMode(R1, OUTPUT); pinMode(R2, OUTPUT);
  pinMode(R3, OUTPUT); pinMode(R4, OUTPUT);
  motorsStop();

  pinMode(BTN_FWD, INPUT_PULLUP);
  pinMode(BTN_REV, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("Dual relay motor ready. Hold BTN_FWD or BTN_REV.");
}

void loop() {
  bool fwd = (digitalRead(BTN_FWD) == LOW);
  bool rev = (digitalRead(BTN_REV) == LOW);

  // If both pressed, do nothing (safety)
  if (fwd && rev) {
    if (currentState != STATE_STOP) {
      motorsStop();
      currentState = STATE_STOP;
      Serial.println("Both buttons pressed - STOP");
    }
    return;
  }

  if (fwd && currentState != STATE_FWD) {
    if (currentState == STATE_REV) {
      // Dead-time before reversing
      motorsStop();
      delay(DEAD_TIME_MS);
    }
    motorsForward();
    currentState = STATE_FWD;
    Serial.println("FORWARD");
  }
  else if (rev && currentState != STATE_REV) {
    if (currentState == STATE_FWD) {
      motorsStop();
      delay(DEAD_TIME_MS);
    }
    motorsReverse();
    currentState = STATE_REV;
    Serial.println("REVERSE");
  }
  else if (!fwd && !rev && currentState != STATE_STOP) {
    motorsStop();
    currentState = STATE_STOP;
    Serial.println("STOP");
  }
}
