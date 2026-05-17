# PC USB-relay bridge for ESP32 <-> Uno serial pass-through.
#
# How it works:
#   ESP32 (COM7 @ 115200) prints lines like ">UNO>@PING" whenever it would
#   normally write to UnoSerial. This script reads those lines, strips the
#   ">UNO>" prefix, and forwards "@PING" out to the Uno (COM4 @ 9600).
#   Replies from the Uno are wrapped in "<UNO<" and written back to ESP32
#   over its USB serial; the ESP32 firmware recognises that prefix and feeds
#   the line into handleUnoLine() as if it had arrived on UnoSerial.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/usb_relay_bridge.ps1
#
# Stop with Ctrl+C.

param(
    [string]$EspPort  = 'COM7',
    [int]   $EspBaud  = 115200,
    [string]$UnoPort  = 'COM4',
    [int]   $UnoBaud  = 9600
)

$ErrorActionPreference = 'Stop'

Write-Host "USB relay bridge starting" -ForegroundColor Cyan
Write-Host "  ESP32: $EspPort @ $EspBaud"
Write-Host "  Uno  : $UnoPort @ $UnoBaud"
Write-Host "  (Ctrl+C to stop)" -ForegroundColor DarkGray
Write-Host ""

$esp = New-Object System.IO.Ports.SerialPort $EspPort, $EspBaud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$uno = New-Object System.IO.Ports.SerialPort $UnoPort, $UnoBaud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$esp.NewLine = "`n"
$uno.NewLine = "`n"
$esp.ReadTimeout = 50
$uno.ReadTimeout = 50
# DTR HIGH on ESP32 holds the chip in run mode (toggling resets it). DTR LOW
# on the Uno avoids the auto-reset that happens when DTR is asserted.
$esp.DtrEnable = $true
$esp.RtsEnable = $true
$uno.DtrEnable = $false
$uno.RtsEnable = $false

try {
    $esp.Open()
    $uno.Open()
} catch {
    Write-Host "Failed to open a serial port: $_" -ForegroundColor Red
    Write-Host "Make sure no other monitor (Arduino IDE / arduino-cli monitor) is attached to the same ports." -ForegroundColor Yellow
    exit 1
}

Write-Host "Bridge running." -ForegroundColor Green

$espBuffer = ''
$unoBuffer = ''

try {
    while ($true) {
        # Drain ESP32 -> look for ">UNO>" lines to forward.
        try {
            while ($esp.BytesToRead -gt 0) {
                $espBuffer += $esp.ReadExisting()
            }
        } catch [System.TimeoutException] { }

        while ($espBuffer.Contains("`n")) {
            $idx = $espBuffer.IndexOf("`n")
            $line = $espBuffer.Substring(0, $idx).TrimEnd("`r")
            $espBuffer = $espBuffer.Substring($idx + 1)

            if ($line.StartsWith('>UNO>')) {
                $payload = $line.Substring(5)
                Write-Host "ESP -> UNO  $payload" -ForegroundColor Cyan
                try {
                    $uno.WriteLine($payload)
                } catch {
                    Write-Host "  write to Uno failed: $_" -ForegroundColor Red
                }
            } else {
                Write-Host "ESP         $line" -ForegroundColor DarkGray
            }
        }

        # Drain Uno -> wrap responses with "<UNO<" and send back to ESP32.
        try {
            while ($uno.BytesToRead -gt 0) {
                $unoBuffer += $uno.ReadExisting()
            }
        } catch [System.TimeoutException] { }

        while ($unoBuffer.Contains("`n")) {
            $idx = $unoBuffer.IndexOf("`n")
            $line = $unoBuffer.Substring(0, $idx).TrimEnd("`r")
            $unoBuffer = $unoBuffer.Substring($idx + 1)

            if ($line.Length -gt 0) {
                Write-Host "UNO -> ESP  $line" -ForegroundColor Yellow
                try {
                    $esp.WriteLine('<UNO<' + $line)
                } catch {
                    Write-Host "  write to ESP failed: $_" -ForegroundColor Red
                }
            }
        }

        Start-Sleep -Milliseconds 10
    }
} finally {
    if ($esp.IsOpen) { $esp.Close() }
    if ($uno.IsOpen) { $uno.Close() }
    Write-Host "Bridge stopped." -ForegroundColor Cyan
}
