"""Dual-port USB bridge: PC sits between Arduino Uno and ESP32.

Architecture:
    Cloud /api/command   --HTTP-->   PC --USB--> ESP32   (handles bills via PCA9685
                                                          servos, parses WITHDRAW)
    ESP32 prints `>UNO>@<cmd>` on USB --PC intercepts--> Uno USB  (coin steppers)
    Uno status replies   --USB-->    PC --USB--> ESP32   (so ESP can track jobs)
    ESP32 events (e.g. `DEPOSIT:20` from bill acceptor) --PC--> /api/deposit
    Uno DEPOSIT events (if bill acceptor wired to Uno)  --PC--> /api/deposit

Override ports with env vars:
    CASH_UNO_PORT   (default COM4)
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

UNO_PORT = os.getenv("CASH_UNO_PORT", "COM4")
ESP_PORT = os.getenv("CASH_ESP_PORT", "COM7")
BAUD_RATE = int(os.getenv("CASH_BAUD", "115200"))

DEPOSIT_API = "https://cashmv.up.railway.app/api/deposit"
COMMAND_API = "https://cashmv.up.railway.app/api/command"
COMMAND_POLL_INTERVAL = 0.5  # seconds between cloud command polls

# Canonical deposit tag emitted by ESP32 (and optionally the Uno).
AMOUNT_FROM_TAG = re.compile(r"DEPOSIT\s*:\s*(-?\d+(?:\.\d+)?)", re.IGNORECASE)

# ESP32 prints lines like ">UNO>@DISPENSE 2 1" whenever it would otherwise
# send to UnoSerial. The bridge strips the prefix and writes to the Uno port.
ESP_TO_UNO_PREFIX = ">UNO>@"

# Uno reply lines we should mirror back to the ESP32 so its state machine
# sees them (it normally reads these from SoftwareSerial). Forward EVERY
# non-empty Uno line by default; only drop lines matching the noise list.
# This makes the PC USB hop a complete substitute for the broken ESP↔Uno
# RX/TX wire — the ESP firmware's handleUnoLine() safely ignores unknowns.
UNO_NOISE_PREFIXES = (
    # PHASE prints fire many times per second during a dispense — skip the
    # spam, the ESP doesn't act on them anyway.
    "PHASE motor=",
)

# Some Uno responses to USB-originated commands carry a USB_/_USB tag that
# the ESP's parser does not recognize. Rewrite them to the canonical form
# before relaying so handleUnoLine() matches the existing branches.
UNO_USB_TO_ESP_REWRITES = (
    ("PONG_USB", "PONG"),
    ("USB_STATUS ", "STATUS "),
    ("USB OK STOP", "OK STOP"),
    ("USB OK ", "OK "),
)


def normalize_uno_line_for_esp(line: str) -> str:
    for src, dst in UNO_USB_TO_ESP_REWRITES:
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


def esp_reader(esp: serial.Serial, uno: serial.Serial, stop_event: threading.Event):
    """Read ESP32 USB lines: forward `>UNO>@...` to Uno, POST DEPOSIT tags."""
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

            if line.startswith(ESP_TO_UNO_PREFIX):
                relayed = line[len(ESP_TO_UNO_PREFIX):]
                print(f"[ESP->UNO] {relayed}")
                safe_write(uno, "UNO", relayed + "\n")
                continue

            amount = extract_amount(line)
            if amount is not None and amount > 0:
                post_deposit(amount, "bill")


def uno_reader(uno: serial.Serial, esp: serial.Serial, stop_event: threading.Event):
    """Read Uno USB lines: mirror reply lines to ESP, POST DEPOSIT tags."""
    buf = bytearray()
    while not stop_event.is_set():
        try:
            chunk = uno.read(256)
        except serial.SerialException as e:
            print(f"[SERIAL-ERROR] UNO read failed: {e}")
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
            print(f"[UNO] {line}")

            # Forward every non-empty Uno line back to the ESP via USB so it
            # substitutes the broken SoftwareSerial wire. Drop only lines
            # that match the noise blocklist.
            if not any(line.startswith(p) for p in UNO_NOISE_PREFIXES):
                relay = normalize_uno_line_for_esp(line)
                print(f"[UNO->ESP] {relay}")
                safe_write(esp, "ESP", "<UNO<" + relay + "\n")

            amount = extract_amount(line)
            if amount is not None and amount > 0:
                post_deposit(amount, "bill")


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
    uno = open_serial(UNO_PORT, "UNO")
    esp = open_serial(ESP_PORT, "ESP")
    stop_event = threading.Event()

    threads = [
        threading.Thread(target=esp_reader, args=(esp, uno, stop_event), daemon=True),
        threading.Thread(target=uno_reader, args=(uno, esp, stop_event), daemon=True),
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
        for s in (uno, esp):
            try:
                s.close()
            except Exception:
                pass


def run():
    print(f"[INFO] Bridge starting. UNO={UNO_PORT}  ESP={ESP_PORT}  BAUD={BAUD_RATE}")
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
