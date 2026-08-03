# Tesla BLE TFT Dashboard

ESP32-S3 Tesla digital dashboard. Connects to Tesla Model 3/Y via BLE for real-time telemetry, rendered on a 320x240 TFT with LVGL.

## Features

- BLE auto-scan, connect, ECDH key authentication
- Real-time speed, gear, regen/consumption visualization
- Door monitoring (VCSEC 320ms fast channel)
- Charging status (power, SOC, time remaining)
- Tire pressure, cabin/outside temperature, battery range
- Scene-driven polling: Driving / Door Open / Charging modes
- Keycard pairing on first use, auto-reconnect on reboot

## Hardware

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| Display | ILI9341 / ST7789 SPI TFT (320x240) |
| Wireless | BLE 4.2+ Central |

## Build

Requires ESP-IDF v5.5.4.

```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

## Configure

Before first use, set your Tesla VIN via the USB config tool:

```bash
pip3 install pyserial
python3 mac_flash_app/esp32_config_gui.py
# or CLI:
python3 mac_flash_app/esp32_config.py set-vin YOUR_TESLA_VIN
python3 mac_flash_app/esp32_config.py save reboot
```

## Credits

- **Tesla BLE Protocol**: [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) v5.1.1
- **LVGL**: Embedded GUI framework v8.4
- **SquareLine Studio**: UI design tool
- **ESP-IDF**: Espressif IoT Development Framework

## License

MIT
