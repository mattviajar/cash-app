# C.A.S.H. Hardware + Software Acceptance Checklist

Use this checklist after deploying to Railway and flashing the latest ESP32 firmware.

## 1) Prerequisites

1. Website deploy is latest commit.
2. Database is migrated/synced.
3. ESP32 is flashed with latest [esp32/cash_atm_controller/cash_atm_controller.ino](../esp32/cash_atm_controller/cash_atm_controller.ino).
4. Uno is flashed with latest [arduino/uno_4_stepper_serial/uno_4_stepper_serial.ino](../arduino/uno_4_stepper_serial/uno_4_stepper_serial.ino).
5. ESP32 Serial Monitor is at 115200.
6. ESP32 and phone are on stable internet.

## 2) Account + Auth Checks

1. Create one parent account.
2. Login as parent.
3. Create two kid accounts.
4. Logout and login as each kid once.

Expected:

1. Parent and kids can log in from different devices.
2. Balances start at PHP 0.00.

## 3) Single-User Device Lock Checks

1. Parent starts a deposit session.
2. While parent session is active, kid tries deposit or withdraw.

Expected:

1. Second user is blocked with device-in-use message.
2. After parent ends session, kid can start a session.

## 4) Deposit Integration Checks

1. Start kid deposit session.
2. Insert one known denomination (for example coin 20).
3. Wait for session finalize.

Expected:

1. Kid balance increases correctly.
2. Inventory endpoint reflects the denomination count.
3. Transaction history shows hardware deposit.

## 5) Withdrawal Integration Checks

1. Ensure kid has enough balance.
2. Start withdrawal session.
3. Request withdraw amount that can be dispensed from current inventory.

Expected:

1. Command is accepted and motors move.
2. Kid balance decreases correctly.
3. Inventory decreases with correct denomination plan.
4. Transaction history records machine withdrawal.

## 6) Fallback Denomination Checks

1. Seed machine with 5x coin 20 and no bill 100.
2. Request withdraw 100.

Expected:

1. System dispenses 5x20 coins equivalent.
2. No inventory underflow.
3. Balance and transaction totals remain correct.

## 7) Negative and Conflict Checks

1. Withdraw with no active lock.
2. Withdraw larger than account balance.
3. Withdraw amount not coverable by inventory.

Expected:

1. Request is rejected safely.
2. No motor action occurs.
3. No incorrect balance or inventory mutation.

## 8) Soak Test (Recommended)

1. Run 20 cycles of mixed deposit and withdrawal.
2. Keep at least two users logged in while only one controls device.

Expected:

1. No duplicate or missing transactions.
2. No negative inventory counts.
3. No deadlock where device stays permanently busy.

## 9) Emergency Recovery

If a test leaves inconsistent hardware state:

1. Stop active session.
2. Release device lock via API or wait TTL expiry.
3. Re-home motors.
4. Re-test with one small amount first.
