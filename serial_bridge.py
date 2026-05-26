"""Dual-port USB bridge: Pi sits between ESP1 and ESP2.

Architecture:
    Cloud /api/command   --HTTP-->   Pi --USB--> ESP1   (withdraw logic + bills)
    ESP1 prints `>ESP2>@<cmd>` on USB --Pi intercepts--> ESP2 USB  (coin motors 1..3)
    ESP2 status replies   --USB-->    Pi --USB--> ESP1   (so ESP1 can track jobs)
    ESP1 events (e.g. `DEPOSIT:20` from bill acceptor) --Pi--> /api/deposit
    ESP2 deposit events   --USB-->    Pi --HTTP--> /api/deposit (if any are emitted)

Override ports with env vars:
    CASH_ESP2_PORT  (default COM4)
    CASH_ESP_PORT   (default COM7)
    CASH_BAUD       (default 115200)
"""

import os
import re
import threading
import time

import requests
import serial
from serial.tools import list_ports

ESP2_PORT = os.getenv("CASH_ESP2_PORT", os.getenv("CASH_UNO_PORT", "COM4"))
ESP_PORT = os.getenv("CASH_ESP_PORT", "COM7")
BAUD_RATE = int(os.getenv("CASH_BAUD", "115200"))

DEPOSIT_API = "https://cashmv.up.railway.app/api/deposit"
COMMAND_API = "https://cashmv.up.railway.app/api/command"
DEVICE_STATUS_API = "https://cashmv.up.railway.app/api/device/status"
COMMAND_POLL_INTERVAL = 0.5  # seconds between cloud command polls

# Canonical deposit tag emitted by ESP controllers.
AMOUNT_FROM_TAG = re.compile(r"DEPOSIT\s*:\s*(-?\d+(?:\.\d+)?)", re.IGNORECASE)
WITHDRAW_AMOUNT_FROM_TAG = re.compile(r"amount\s*=\s*(\d+)", re.IGNORECASE)

# ESP1 prints lines like ">ESP2>@DISPENSE 2 1" for ESP2-bound commands.
ESP_TO_ESP2_PREFIX = ">ESP2>@"

# ESP2 reply lines we should mirror back to ESP1 so its state machine sees
# them. Forward every non-empty line by default; only drop noise lines.
# This makes the Pi USB hop a complete inter-device relay between ESP1 and ESP2.
ESP2_NOISE_PREFIXES = (
    # PHASE prints fire many times per second during a dispense — skip the
    # spam, the ESP doesn't act on them anyway.
    "PHASE motor=",
)

# Some ESP2 responses to USB-originated commands carry a USB_/_USB tag that
# the ESP's parser does not recognize. Rewrite them to the canonical form
# before relaying so ESP1's parser matches the existing branches.
ESP2_USB_TO_ESP1_REWRITES = (
    ("PONG_USB", "PONG"),
    ("USB_STATUS ", "STATUS "),
    ("USB OK STOP", "OK STOP"),
    ("USB OK ", "OK "),
)


def normalize_esp2_line_for_esp1(line: str) -> str:
    for src, dst in ESP2_USB_TO_ESP1_REWRITES:
        if line.startswith(src):
            return dst + line[len(src):]
    return line


def extract_amount(line: str):
    m = AMOUNT_FROM_TAG.search(line)
    if m:
        try:
            return float(m.group(1))
        except ValueError:
            return None
    return None


def post_deposit(amount: float, source: str):
    try:
        r = requests.post(
            DEPOSIT_API,
            json={"amount": amount, "source": source},
            timeout=5,
        )
        print(f"[POST] amount={amount} src={source} status={r.status_code} body={r.text}")
    except Exception as e:
        print(f"[POST-ERROR] amount={amount} src={source} error={e}")


def post_withdraw_status(state: str, amount: int, active: bool):
    try:
        r = requests.post(
            DEVICE_STATUS_API,
            json={
                "withdrawActive": active,
                "withdrawState": state,
                "withdrawAmount": amount,
            },
            timeout=5,
        )
        print(
            f"[POST-STATUS] state={state} active={int(active)} amount={amount} "
            f"status={r.status_code} body={r.text}"
        )
    except Exception as e:
        print(f"[POST-STATUS-ERROR] state={state} amount={amount} error={e}")


def maybe_post_withdraw_status(line: str):
    upper = line.upper()

    if upper.startswith("WITHDRAW START"):
        amount = 0
        m = WITHDRAW_AMOUNT_FROM_TAG.search(line)
        if m:
            try:
                amount = int(m.group(1))
            except ValueError:
                amount = 0
        post_withdraw_status("dispensing", amount, True)
        return

    if upper.startswith("WITHDRAW DONE"):
        post_withdraw_status("complete", 0, False)
        return

    if upper.startswith("WITHDRAW ERR") or upper.startswith("ERR WITHDRAW"):
        post_withdraw_status("error", 0, False)
        return


def available_ports():
    return [p.device for p in list_ports.comports()]


def open_serial(port: str, label: str):
    """Block until the named port can be opened."""
    while True:
        try:
            ser = serial.Serial(port, BAUD_RATE, timeout=0.2)
            print(f"[INFO] {label} connected on {port} @ {BAUD_RATE}")
            return ser
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] {label} on {port}: {e}")
            ports = available_ports()
            print(f"[INFO] Available COM ports: {', '.join(ports) if ports else '(none)'}")
            print(f"[INFO] Retrying {label} in 2s...")
            time.sleep(2)


def safe_write(ser: serial.Serial, label: str, data: str):
    try:
        ser.write(data.encode())
    except serial.SerialException as e:
        print(f"[SERIAL-ERROR] write to {label} failed: {e}")


def esp_reader(esp: serial.Serial, esp2: serial.Serial, stop_event: threading.Event):
    """Read ESP1 USB lines: forward `>ESP2>@...` to ESP2, POST DEPOSIT tags."""
    buf = bytearray()
    while not stop_event.is_set():
        try:
            chunk = esp.read(256)
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] ESP read failed: {e}")
            stop_event.set()
            return
        if not chunk:
            continue
        buf.extend(chunk)
        while True:
            nl = buf.find(b"\n")
            if nl < 0:
                break
            raw = bytes(buf[:nl])
            del buf[: nl + 1]
            line = raw.decode(errors="ignore").strip()
            if not line:
                continue
            print(f"[ESP] {line}")

            if line.startswith(ESP_TO_ESP2_PREFIX):
                relayed = line[len(ESP_TO_ESP2_PREFIX):]
                print(f"[ESP->ESP2] {relayed}")
                safe_write(esp2, "ESP2", relayed + "\n")
                continue

            amount = extract_amount(line)
            if amount is not None and amount > 0:
                post_deposit(amount, "bill")

            maybe_post_withdraw_status(line)


def esp2_reader(esp2: serial.Serial, esp: serial.Serial, stop_event: threading.Event):
    """Read ESP2 USB lines: mirror reply lines to ESP1, POST DEPOSIT tags."""
    buf = bytearray()
    while not stop_event.is_set():
        try:
            chunk = esp2.read(256)
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] ESP2 read failed: {e}")
            stop_event.set()
            return
        if not chunk:
            continue
        buf.extend(chunk)
        while True:
            nl = buf.find(b"\n")
            if nl < 0:
                break
            raw = bytes(buf[:nl])
            del buf[: nl + 1]
            line = raw.decode(errors="ignore").strip()
            if not line:
                continue
            print(f"[ESP2] {line}")

            # Forward every non-empty ESP2 line back to ESP1 via USB so it
            # substitutes the broken SoftwareSerial wire. Drop only lines
            # that match the noise blocklist.
            if not any(line.startswith(p) for p in ESP2_NOISE_PREFIXES):
                relay = normalize_esp2_line_for_esp1(line)
                print(f"[ESP2->ESP1] {relay}")
                safe_write(esp, "ESP", "<ESP2<" + relay + "\n")

            amount = extract_amount(line)
            if amount is not None and amount > 0:
                post_deposit(amount, "coin")


def command_poll_loop(esp: serial.Serial, stop_event: threading.Event):
    """Drain the cloud command queue and write each command to the ESP32 USB.

    ESP32 firmware's readUsbCommands() will treat them identically to cloud-
    fetched commands and run the full WITHDRAW state machine.
    """
    while not stop_event.is_set():
        try:
            r = requests.get(f"{COMMAND_API}?consume=true", timeout=3)
            data = r.json()
            for cmd in data.get("commands", []):
                cmd = (cmd or "").strip()
                if not cmd:
                    continue
                print(f"[CMD->ESP] {cmd}")
                safe_write(esp, "ESP", cmd + "\n")
        except Exception as e:
            print(f"[CMD-ERROR] {e}")
        for _ in range(int(COMMAND_POLL_INTERVAL * 10)):
            if stop_event.is_set():
                return
            time.sleep(0.1)


def run_once():
    esp2 = open_serial(ESP2_PORT, "ESP2")
    esp = open_serial(ESP_PORT, "ESP")
    stop_event = threading.Event()

    threads = [
        threading.Thread(target=esp_reader, args=(esp, esp2, stop_event), daemon=True),
        threading.Thread(target=esp2_reader, args=(esp2, esp, stop_event), daemon=True),
        threading.Thread(target=command_poll_loop, args=(esp, stop_event), daemon=True),
    ]
    for t in threads:
        t.start()

    try:
        while not stop_event.is_set():
            time.sleep(0.25)
    except KeyboardInterrupt:
        print("[INFO] Bridge stopped (Ctrl+C).")
        stop_event.set()
        raise
    finally:
        stop_event.set()
        for s in (esp2, esp):
            try:
                s.close()
            except Exception:
                pass


def run():
    print(f"[INFO] Bridge starting. ESP2={ESP2_PORT}  ESP1={ESP_PORT}  BAUD={BAUD_RATE}")
    while True:
        try:
            run_once()
            print("[INFO] Session ended; reconnecting in 2s...")
            time.sleep(2)
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[ERROR] {e}")
            print("[INFO] Restarting in 2 seconds...")
            time.sleep(2)


if __name__ == "__main__":
    run()
