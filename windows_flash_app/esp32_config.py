#!/usr/bin/env python3
"""
ESP32 Tesla BLE Dashboard — USB Configuration Tool (Windows CLI)
================================================================
Write VIN, display model, and SPI pins to ESP32 NVS via USB serial.
No recompilation needed.

Requires: pip install pyserial

Usage:
  esp32_config.exe status              # Show current config
  esp32_config.exe set-vin <17 chars>  # Set VIN
  esp32_config.exe set-model <name>    # Set display model
  esp32_config.exe set-pins <7 vals>   # Set SPI pins
  esp32_config.exe preset <name>       # Apply preset
  esp32_config.exe save                # Save to NVS
  esp32_config.exe reboot              # Save + reboot
  esp32_config.exe ports               # List available serial ports
  esp32_config.exe --port COM7 ...     # Specify port manually
"""

import argparse
import sys
import time
import serial
import serial.tools.list_ports

# Windows consoles often use GBK (cp936); force UTF-8 output so Chinese
# port descriptions don't garble in modern terminals (WT / git-bash).
if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# ── Presets ──────────────────────────────────────────────────
PRESETS = {
    "2.8inch-ESP32-S3": {
        "model": "ILI9341",
        "pins":  "12,11,13,46,10,-1,45",  # SCK,MOSI,MISO,DC,CS,RST,BLK
        "desc":  "2.8inch ESP32-S3 Display (ILI9341, 320x240)"
    },
}

# ── Serial helpers ──────────────────────────────────────────
BAUD = 115200
TIMEOUT = 2.0

def find_port():
    """Auto-detect ESP32 serial port on Windows."""
    ports = list(serial.tools.list_ports.comports())
    # Espressif native USB (VID 303A) — exact match first
    for p in ports:
        if "303A" in (p.hwid or "").upper():
            return p.device
    for p in ports:
        # Windows: COM ports with CP210x or CH340 chips
        if any(x in p.description for x in ("CP210", "CH340", "ESP32", "USB Serial", "USB-SERIAL", "Silicon Labs")):
            return p.device
    # Fallback: first available COM port
    for p in ports:
        if "COM" in p.device:
            return p.device
    return None

def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device:12s} {p.description}")

def send_command(ser, cmd, timeout=2.0):
    """Send a command and read all response lines until timeout."""
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    lines = []
    t0 = time.time()
    while time.time() - t0 < timeout:
        if ser.in_waiting:
            try:
                line = ser.readline().decode(errors='replace').strip()
                if line:
                    lines.append(line)
            except Exception:
                break
        else:
            time.sleep(0.02)
    return lines

def validate_vin(vin):
    vin = vin.strip().upper()
    if len(vin) != 17:
        raise ValueError(f"VIN must be 17 characters (got {len(vin)})")
    if not all(c.isalnum() for c in vin):
        raise ValueError("VIN must contain only letters and digits")
    return vin

def validate_pins(pins_str):
    parts = [x.strip() for x in pins_str.split(",")]
    if len(parts) != 7:
        raise ValueError(f"Need 7 values: SCK,MOSI,MISO,DC,CS,RST,BLK (got {len(parts)})")
    for x in parts:
        try:
            int(x)
        except ValueError:
            raise ValueError(f"Invalid pin value: '{x}' (must be an integer, -1 = not connected)")
    return ",".join(parts)

# ── Commands ─────────────────────────────────────────────────
def cmd_status(ser):
    for l in send_command(ser, "STATUS"):
        print(l)

def cmd_set_vin(ser, vin):
    vin = validate_vin(vin)
    for l in send_command(ser, f"SET VIN={vin}"):
        print(l)

def cmd_set_model(ser, model):
    for l in send_command(ser, f"SET MODEL={model.strip()}"):
        print(l)

def cmd_set_pins(ser, pins_str):
    pins_str = validate_pins(pins_str)
    for l in send_command(ser, f"SET PINS={pins_str}"):
        print(l)

def cmd_preset(ser, name):
    p = PRESETS[name]
    print(f"Applying preset: {p['desc']}")
    send_command(ser, f"SET MODEL={p['model']}", timeout=1.0)
    for l in send_command(ser, f"SET PINS={p['pins']}", timeout=1.0):
        print(l)
    print("Done. Use 'save' to persist.")

def cmd_save(ser):
    for l in send_command(ser, "SAVE"):
        print(l)

def cmd_reboot(ser):
    for l in send_command(ser, "SAVE"):
        print(l)
    time.sleep(0.5)
    send_command(ser, "REBOOT", timeout=0.5)
    print("Rebooting...")

# ── Main ─────────────────────────────────────────────────────
def build_parser():
    parser = argparse.ArgumentParser(
        prog="esp32_config",
        description="ESP32 Tesla BLE Dashboard USB config tool (Windows)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("--port", metavar="COMx",
                        help="serial port (default: auto-detect)")
    sub = parser.add_subparsers(dest="command", metavar="COMMAND")

    sub.add_parser("status", help="show current config on device")

    p = sub.add_parser("set-vin", help="set vehicle VIN (17 characters)")
    p.add_argument("vin", metavar="VIN")

    p = sub.add_parser("set-model", help="set display model (e.g. ILI9341)")
    p.add_argument("model", metavar="MODEL")

    p = sub.add_parser("set-pins", help="set SPI pins: SCK,MOSI,MISO,DC,CS,RST,BLK")
    p.add_argument("pins", metavar="PINS",
                   help="7 comma-separated integers, -1 = not connected")

    p = sub.add_parser("preset", help="apply a board preset")
    p.add_argument("name", metavar="NAME", choices=sorted(PRESETS),
                   help="available: " + ", ".join(sorted(PRESETS)))

    sub.add_parser("save", help="save config to NVS")
    sub.add_parser("reboot", help="save config and reboot ESP32")
    sub.add_parser("ports", help="list available serial ports")
    return parser

def main():
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "ports":
        list_ports()
        return

    port = args.port or find_port()
    if port is None:
        print("ERROR: No serial port found. Use --port COM7 to specify manually.")
        print("Tip: run 'ports' to list available serial ports.")
        sys.exit(1)

    print(f"Using port: {port}")
    try:
        ser = serial.Serial(port, BAUD, timeout=TIMEOUT)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {port}: {e}")
        sys.exit(1)
    time.sleep(1.5)

    try:
        if args.command == "status":
            cmd_status(ser)
        elif args.command == "set-vin":
            cmd_set_vin(ser, args.vin)
        elif args.command == "set-model":
            cmd_set_model(ser, args.model)
        elif args.command == "set-pins":
            cmd_set_pins(ser, args.pins)
        elif args.command == "preset":
            cmd_preset(ser, args.name)
        elif args.command == "save":
            cmd_save(ser)
        elif args.command == "reboot":
            cmd_reboot(ser)
    except ValueError as e:
        print(f"ERROR: {e}")
        sys.exit(1)
    finally:
        ser.close()

if __name__ == "__main__":
    main()
