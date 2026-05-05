import re
import time
import os
import requests
import serial
from serial.tools import list_ports

# Serial source and API target
# Set CASH_SERIAL_PORT env var to override, e.g. CASH_SERIAL_PORT=COM8
COM_PORT = os.getenv("CASH_SERIAL_PORT", "COM6")
BAUD_RATE = 115200
DEPOSIT_API  = "https://cash-app-production-458e.up.railway.app/api/deposit"
COMMAND_API  = "https://cash-app-production-458e.up.railway.app/api/command"
COMMAND_POLL_INTERVAL = 0.5  # seconds between command polls

# Accept only canonical deposit tag lines from firmware.
AMOUNT_FROM_TAG = re.compile(r"DEPOSIT\s*:\s*(-?\d+(?:\.\d+)?)", re.IGNORECASE)

def extract_amount(line: str):
    m = AMOUNT_FROM_TAG.search(line)
    if m:
        return float(m.group(1))
    return None

def post_deposit(amount: float):
    try:
        r = requests.post(DEPOSIT_API, json={"amount": amount}, timeout=5)
        print(f"[POST] amount={amount} status={r.status_code} body={r.text}")
    except Exception as e:
        print(f"[POST-ERROR] amount={amount} error={e}")

def available_ports():
    return [p.device for p in list_ports.comports()]

def poll_commands(ser):
    """Drain the command queue and write each command to serial."""
    try:
        r = requests.get(f"{COMMAND_API}?consume=true", timeout=2)
        data = r.json()
        for cmd in data.get("commands", []):
            cmd = cmd.strip()
            if cmd:
                print(f"[CMD] → {cmd}")
                ser.write((cmd + "\n").encode())
    except Exception as e:
        print(f"[CMD-ERROR] {e}")

def run():
    print(f"[INFO] Opening serial {COM_PORT} @ {BAUD_RATE}")
    while True:
        try:
            # Reopen serial on every retry so unplug/replug is handled automatically.
            with serial.Serial(COM_PORT, BAUD_RATE, timeout=1) as ser:
                print("[INFO] Serial connected. Waiting for events...")
                last_cmd_poll = 0.0
                while True:
                    raw = ser.readline()
                    if raw:
                        line = raw.decode(errors="ignore").strip()
                        if line:
                            print(f"[SERIAL] {line}")
                            amount = extract_amount(line)
                            if amount is not None and amount > 0:
                                post_deposit(amount)

                    # Poll for outbound commands on a timer.
                    now = time.time()
                    if now - last_cmd_poll >= COMMAND_POLL_INTERVAL:
                        last_cmd_poll = now
                        poll_commands(ser)
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] {e}")
            ports = available_ports()
            if ports:
                print(f"[INFO] Available COM ports: {', '.join(ports)}")
                print("[INFO] If needed, restart with: CASH_SERIAL_PORT=<PORT> py -3 serial_bridge.py")
            else:
                print("[INFO] No COM ports detected. Check USB cable/power.")
            print("[INFO] Retrying in 2 seconds...")
            time.sleep(2)
        except KeyboardInterrupt:
            print("[INFO] Bridge stopped.")
            break
        except Exception as e:
            print(f"[ERROR] {e}")
            print("[INFO] Retrying in 2 seconds...")
            time.sleep(2)

if __name__ == "__main__":
    run()
