# Thesis Deployment Checklist

## 1. Firmware Flashing

### ESP32 #1: `cash_atm_controller`
- **Sketch**: `esp32/cash_atm_controller/cash_atm_controller.ino`
- **Purpose**: Main ATM controller for bills and coins.
- **Steps**:
  1. Open the sketch in Arduino IDE.
  2. Set the correct board: `ESP32 Dev Module`.
  3. Set the upload speed: `115200`.
  4. Update Wi-Fi credentials in the sketch:
     ```cpp
     constexpr char WIFI_SSID[] = "<Your WiFi SSID>";
     constexpr char WIFI_PASSWORD[] = "<Your WiFi Password>";
     ```
  5. Flash the firmware to the ESP32.

### ESP32 #2: `coin_servo_controller`
- **Sketch**: `esp32/coin_servo_controller/coin_servo_controller.ino`
- **Purpose**: Coin dispenser controller.
- **Steps**:
  1. Open the sketch in Arduino IDE.
  2. Set the correct board: `ESP32 Dev Module`.
  3. Set the upload speed: `115200`.
  4. Update the pin map in the sketch:
     ```cpp
     MotorConfig MOTOR_CFG[MOTOR_COUNT] = {
       {<servoPin1>, <irPin1>, true, 1700, 1300, 1500, 300, 12000, 1},
       {<servoPin2>, <irPin2>, true, 1700, 1300, 1500, 300, 12000, 5},
       {<servoPin3>, <irPin3>, true, 1700, 1300, 1500, 300, 12000, 10},
       {<servoPin4>, <irPin4>, true, 1700, 1300, 1500, 300, 12000, 20},
     };
     ```
  5. Flash the firmware to the ESP32.

## 2. Web App Deployment

### Local Development
- **Steps**:
  1. Install dependencies:
     ```bash
     npm install
     ```
  2. Run the development server:
     ```bash
     npm run dev
     ```
  3. Open the app in your browser: [http://localhost:3001](http://localhost:3001).

### Production Deployment
- **Steps**:
  1. Build the app:
     ```bash
     npm run build
     ```
  2. Start the production server:
     ```bash
     npm start
     ```
  3. Ensure the app is accessible at the configured domain.

## 3. Environment Variables
- **File**: `.env`
- **Variables**:
  ```env
  DATABASE_URL=<Your Prisma Database URL>
  FIREBASE_API_KEY=<Your Firebase API Key>
  FIREBASE_AUTH_DOMAIN=<Your Firebase Auth Domain>
  FIREBASE_PROJECT_ID=<Your Firebase Project ID>
  FIREBASE_STORAGE_BUCKET=<Your Firebase Storage Bucket>
  FIREBASE_MESSAGING_SENDER_ID=<Your Firebase Messaging Sender ID>
  FIREBASE_APP_ID=<Your Firebase App ID>
  ```

## 4. End-to-End Test Flow

### Hardware Tests
1. Power on both ESP32 boards.
2. Verify Wi-Fi connection:
   - Check the serial monitor for `Wi-Fi connected` messages.
   - Note the IP addresses of both boards.
3. Test coin dispensing:
   - Send the `DISPENSE` command via serial:
     ```
     DISPENSE <motor> <count>
     ```
   - Verify the correct number of coins are dispensed.
4. Test bill acceptance:
   - Insert a bill and check the serial monitor for `DEPOSIT` events.

### Web App Tests
1. Open the app in your browser.
2. Log in as a parent and a child.
3. Perform the following actions:
   - Deposit coins and bills.
   - Withdraw cash.
   - Check the dashboard for real-time updates.

### Integration Tests
1. Verify deposits appear in the database.
2. Verify withdrawals trigger the correct hardware actions.
3. Check the `device/status` API for accurate state updates.

## 5. Final Validation
- Ensure all components are powered and connected.
- Perform a full demo run:
  1. Deposit coins and bills.
  2. Withdraw cash.
  3. Verify the dashboard reflects all transactions.

---

**Notes**:
- Use the `serial_bridge.py` script for debugging ESP32↔Uno communication.
- Ensure the `.env` file is not committed to GitHub.