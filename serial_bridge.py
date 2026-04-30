import re
import time
import requests
import serial

# Serial source and API target
COM_PORT = "COM6"
BAUD_RATE = 115200
DEPOSIT_API = "http://localhost:3001/api/deposit"

# Accept either verbose firmware logs or compact DEPOSIT tags
AMOUNT_FROM_LOG = re.compile(r"amount:\s*(\d+)", re.IGNORECASE)
AMOUNT_FROM_TAG = re.compile(r"DEPOSIT:\s*(\d+)", re.IGNORECASE)

def extract_amount(line: str):
    m = AMOUNT_FROM_LOG.search(line)
    if m:
        return int(m.group(1))
    m = AMOUNT_FROM_TAG.search(line)
    if m:
        return int(m.group(1))
    return None

def post_deposit(amount: int):
    try:
        r = requests.post(DEPOSIT_API, json={"amount": amount}, timeout=5)
        print(f"[POST] amount={amount} status={r.status_code} body={r.text}")
    except Exception as e:
        print(f"[POST-ERROR] amount={amount} error={e}")

def run():
    print(f"[INFO] Opening serial {COM_PORT} @ {BAUD_RATE}")
    while True:
        try:
            # Reopen serial on every retry so unplug/replug is handled automatically.
            with serial.Serial(COM_PORT, BAUD_RATE, timeout=1) as ser:
                print("[INFO] Serial connected. Waiting for coin events...")
                while True:
                    raw = ser.readline()
                    if not raw:
                        continue
                    line = raw.decode(errors="ignore").strip()
                    if not line:
                        continue
                    print(f"[SERIAL] {line}")
                    amount = extract_amount(line)
                    if amount is not None and amount > 0:
                        post_deposit(amount)
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] {e}")
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
