#!/usr/bin/env python3
"""
ESP32 Tesla BLE Dashboard — USB Configuration Tool (Windows)
=============================================================
Write VIN, display model, and SPI pins to ESP32 NVS via USB serial.
No recompilation needed.

Requires: pip install pyserial

Usage:
  python esp32_config.py status              # Show current config
  python esp32_config.py set-vin <17 chars>  # Set VIN
  python esp32_config.py set-model <name>    # Set display model
  python esp32_config.py set-pins <7 vals>   # Set SPI pins
  python esp32_config.py preset <name>       # Apply preset
  python esp32_config.py save                # Save to NVS
  python esp32_config.py reboot              # Save + reboot
  python esp32_config.py --port COM7 ...     # Specify port manually
"""

import sys
import time
import serial
import serial.tools.list_ports

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
    for p in ports:
        # Windows: COM ports with CP210x or CH340 chips
        if any(x in p.description for x in ("CP210", "CH340", "ESP32", "USB Serial", "Silicon Labs")):
            return p.device
    # Fallback: first available COM port
    for p in ports:
        if "COM" in p.device:
            return p.device
    return None

def send_command(ser, cmd, wait=0.5):
    """Send a command and read response until timeout."""
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        try:
            line = ser.readline().decode(errors='replace').strip()
            if line:
                lines.append(line)
        except:
            break
    return lines

# ── Commands ─────────────────────────────────────────────────
def cmd_status(ser):
    lines = send_command(ser, "STATUS", wait=1.0)
    for l in lines:
        print(l)

def cmd_set_vin(ser, vin):
    if len(vin) != 17:
        print(f"ERROR: VIN must be 17 characters (got {len(vin)})")
        sys.exit(1)
    lines = send_command(ser, f"SET VIN={vin}")
    for l in lines:
        print(l)

def cmd_set_model(ser, model):
    lines = send_command(ser, f"SET MODEL={model}")
    for l in lines:
        print(l)

def cmd_set_pins(ser, pins_str):
    parts = pins_str.split(",")
    if len(parts) != 7:
        print(f"ERROR: Need 7 values: SCK,MOSI,MISO,DC,CS,RST,BLK (got {len(parts)})")
        sys.exit(1)
    lines = send_command(ser, f"SET PINS={pins_str}")
    for l in lines:
        print(l)

def cmd_preset(ser, name):
    if name not in PRESETS:
        print(f"Unknown preset '{name}'. Available: {list(PRESETS.keys())}")
        sys.exit(1)
    p = PRESETS[name]
    print(f"Applying preset: {p['desc']}")
    send_command(ser, f"SET MODEL={p['model']}", wait=0.3)
    send_command(ser, f"SET PINS={p['pins']}", wait=0.3)
    print("Done. Use 'save' to persist.")

def cmd_save(ser):
    lines = send_command(ser, "SAVE")
    for l in lines:
        print(l)

def cmd_reboot(ser):
    send_command(ser, "SAVE")
    time.sleep(0.5)
    send_command(ser, "REBOOT", wait=0.3)
    print("Rebooting...")

# ── Main ─────────────────────────────────────────────────────
def main():
    # Parse --port flag
    port = None
    args = [a for a in sys.argv[1:] if not a.startswith("--port")]
    for a in sys.argv[1:]:
        if a.startswith("--port="):
            port = a.split("=", 1)[1]
        elif a == "--port" and sys.argv.index(a) + 1 < len(sys.argv):
            port = sys.argv[sys.argv.index(a) + 1]

    if not args:
        print(__doc__)
        print("\nAvailable presets:")
        for name, p in PRESETS.items():
            print(f"  {name:25s} {p['desc']}")
        sys.exit(1)

    cmd = args[0]

    if port is None:
        port = find_port()
    if port is None:
        print("ERROR: No serial port found. Use --port COM7 to specify manually.")
        sys.exit(1)

    print(f"Using port: {port}")
    ser = serial.Serial(port, BAUD, timeout=TIMEOUT)
    time.sleep(1.5)

    if cmd == "status":
        cmd_status(ser)
    elif cmd == "set-vin" and len(args) >= 2:
        cmd_set_vin(ser, args[1])
    elif cmd == "set-model" and len(args) >= 2:
        cmd_set_model(ser, args[1])
    elif cmd == "set-pins" and len(args) >= 2:
        cmd_set_pins(ser, args[1])
    elif cmd == "preset" and len(args) >= 2:
        cmd_preset(ser, args[1])
    elif cmd == "save":
        cmd_save(ser)
    elif cmd == "reboot":
        cmd_reboot(ser)
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)

    ser.close()

if __name__ == "__main__":
    main()
