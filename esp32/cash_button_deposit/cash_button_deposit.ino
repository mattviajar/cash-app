#include <WiFi.h>
#include <HTTPClient.h>

// Wi-Fi credentials
const char* WIFI_SSID = "Duke&Duchess";
const char* WIFI_PASSWORD = "HappyFamily123!";

// IMPORTANT: Use your PC/Laptop LAN IP here, not localhost.
// Example: http://192.168.1.50:3001/api/deposit/hardware
const char* DEPOSIT_API = "http://192.168.1.4:3001/api/deposit/hardware";

const int BTN_ADD_10 = 32;
const int BTN_2 = 33; // Subtract 100 action

// Active-low buttons with INPUT_PULLUP
bool lastAdd100Pressed = false;
bool lastBtn2Pressed = false;
unsigned long lastDebounceMs = 0;
const unsigned long debounceWindowMs = 40;

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(350);
    Serial.print('.');
  }
  Serial.println(" connected");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void postButtonEvent(const char* button) {
  ensureWifi();

  HTTPClient http;
  http.begin(DEPOSIT_API);
  http.addHeader("Content-Type", "application/json");

  String body = String("{\"button\":\"") + button + "\"}";
  int status = http.POST(body);

  Serial.print("POST button=");
  Serial.print(button);
  Serial.print(" status=");
  Serial.println(status);

  if (status > 0) {
    String response = http.getString();
    Serial.println(response);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_ADD_10, INPUT_PULLUP);
  pinMode(BTN_2, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  ensureWifi();
}

void loop() {
  bool add10Pressed = (digitalRead(BTN_ADD_10) == LOW);
  bool btn2Pressed = (digitalRead(BTN_2) == LOW);

  unsigned long now = millis();

  // Edge trigger + debounce: fire once when button is pressed.
  if (add10Pressed && !lastAdd100Pressed && (now - lastDebounceMs) > debounceWindowMs) {
    postButtonEvent("add10");
    lastDebounceMs = now;
  }

  // Second button subtracts 10.
  if (btn2Pressed && !lastBtn2Pressed && (now - lastDebounceMs) > debounceWindowMs) {
    postButtonEvent("subtract10");
    lastDebounceMs = now;
  }

  lastAdd100Pressed = add10Pressed;
  lastBtn2Pressed = btn2Pressed;

  delay(5);
}
