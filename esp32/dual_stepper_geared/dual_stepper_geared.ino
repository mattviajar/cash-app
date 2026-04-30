/*
 * Arduino (UNO/Nano) IR Proximity Sensor Test
 *
 * Reads both outputs of an IR obstacle/proximity module:
 *   D0 -> digital pin 2 (HIGH = no object, LOW = object detected)
 *   A0 -> analog pin A0 (0-1023, lower = closer object)
 *
 * Wiring:
 *   Sensor D0  -> D2
 *   Sensor A0  -> A0
 *   Sensor VCC -> 5V
 *   Sensor GND -> GND
 */

const int D0_PIN = 2;
const int A0_PIN = A0;

void setup() {
  Serial.begin(9600);
  pinMode(D0_PIN, INPUT);
  Serial.println("IR sensor test ready.");
  Serial.println("D0 (digital) | A0 (analog 0-1023)");
  Serial.println("------------------------------------");
}

void loop() {
  int digital = digitalRead(D0_PIN);
  int analog  = analogRead(A0_PIN);

  Serial.print("D0: ");
  Serial.print(digital == LOW ? "DETECTED" : "clear   ");
  Serial.print("  |  A0: ");
  Serial.println(analog);

  delay(200);
}
