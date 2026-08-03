# Tesla BLE TFT Dashboard

ESP32-S3 open-source Tesla digital dashboard. Connects to Tesla Model 3/Y via BLE, renders real-time telemetry on a 320x240 TFT with LVGL.

[中文说明](README_CN.md)

## Features

- BLE auto-scan, connect, ECDH key authentication
- Real-time speed, gear, regen/consumption visualization
- Door monitoring via VCSEC 320ms fast channel
- Charging status (power, SOC, time remaining)
- Tire pressure, cabin/outside temperature, battery range
- Scene-driven polling: Driving / Door Open / Charging modes
- Keycard pairing on first use, auto-reconnect on reboot

## Hardware

![Dev Board](doc/IMG_7255.jpg)

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| Display | ILI9341 / ST7789 SPI TFT (320x240) |
| Wireless | BLE 4.2+ Central |

## Quick Start

### Option 1: Flash pre-built binary (recommended)

1. Download `tesla_ble_dash.bin` from [Releases](https://github.com/elementchen/TESLA_BLE_TFT/releases)
2. Download [Espressif Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
3. Connect ESP32-S3, select chip `ESP32-S3`, load files:

| File | Address |
|------|---------|
| `bootloader.bin` | `0x0000` |
| `partition-table.bin` | `0x8000` |
| `tesla_ble_dash.bin` | `0x20000` |

4. SPI settings: **80MHz, QIO, 16MB Flash**. Click START.

### Option 2: Build from source

Requires ESP-IDF v5.5.4.

```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

## After Flashing — Configure Your Device

The firmware needs your **Tesla VIN** and **display pinout** to work. Use the USB config tool:

**macOS (GUI):**
```bash
pip3 install pyserial
python3 mac_flash_app/esp32_config_gui.py
```

**macOS / Windows (CLI):**
```bash
pip install pyserial
python3 mac_flash_app/esp32_config.py set-vin YOUR_17_CHAR_VIN
python3 mac_flash_app/esp32_config.py save reboot
```

The tool has built-in presets for common boards (2.8inch ESP32-S3 ILI9341, ST7789). If your board has different pinout, adjust the 7 SPI pin values in the GUI.

## Pairing

After reboot, the display shows "TAP CARD". Tap your Tesla keycard on the center console to authorize. Once paired, the ESP32 reconnects automatically on every startup.

## Credits

- **Tesla BLE Protocol**: [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) v5.1.1
- **LVGL**: Embedded GUI framework v8.4
- **SquareLine Studio**: UI design tool
- **ESP-IDF**: Espressif IoT Development Framework

## License

MIT
