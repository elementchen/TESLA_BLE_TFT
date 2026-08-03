# Tesla BLE TFT Dashboard / 特斯拉 BLE TFT 仪表盘

<sub>ESP32-S3 · LVGL 8 · Tesla BLE Protocol | [English](#english) | [中文](#中文)</sub>

---

<a name="english"></a>
## English

ESP32-S3 open-source Tesla digital dashboard. Connects to Tesla Model 3/Y via BLE, renders real-time telemetry on a 320x240 TFT with LVGL.

### Features

- BLE auto-scan, connect, ECDH key authentication
- Real-time speed, gear, regen/consumption visualization
- Door monitoring via VCSEC 320ms fast channel
- Charging status (power, SOC, time remaining)
- Tire pressure, cabin/outside temperature, battery range
- Scene-driven polling: Driving / Door Open / Charging modes
- Keycard pairing on first use, auto-reconnect on reboot

### Hardware

![Dev Board](doc/IMG_7255.jpg)

| Component | Spec |
|-----------|------|
| MCU | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| Display | ILI9341 / ST7789 SPI TFT (320x240) |
| Wireless | BLE 4.2+ Central |

### Quick Start

**Option 1: Flash pre-built binary (recommended)**

1. Download `tesla_ble_dash.bin` from [Releases](https://github.com/elementchen/TESLA_BLE_TFT/releases)
2. Download [Espressif Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)
3. Connect ESP32-S3, select chip `ESP32-S3`, load files at addresses:

| File | Address |
|------|---------|
| `bootloader.bin` | `0x0000` |
| `partition-table.bin` | `0x8000` |
| `tesla_ble_dash.bin` | `0x20000` |

4. SPI settings: **80MHz, QIO, 16MB Flash**. Click START.

**Option 2: Build from source**

```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

### After Flashing — Configure

The firmware needs your **Tesla VIN** and **display pinout**. Use the USB config tool:

| Platform | Command |
|----------|---------|
| **macOS GUI** | Download `ESP32_Config_Tool_macOS.zip` from Releases, unzip and double-click |
| **Windows EXE** | Download `ESP32_Config_Tool.exe` from Releases |
| **CLI** | `pip install pyserial && python3 mac_flash_app/esp32_config.py set-vin YOUR_VIN` |

Built-in presets for common boards (2.8inch ESP32-S3 ILI9341, ST7789).

### Pairing

After reboot, the display shows "TAP CARD". Tap your Tesla keycard on the center console to authorize. Once paired, the ESP32 reconnects automatically on every startup.

### Credits

- **Tesla BLE Protocol**: [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) v5.1.1
- **LVGL**: Embedded GUI framework v8.4
- **SquareLine Studio**: UI design tool
- **ESP-IDF**: Espressif IoT Development Framework

### License

MIT

---

<a name="中文"></a>
## 中文

基于 ESP32-S3 的开源特斯拉数字仪表盘。通过 BLE 连接 Tesla Model 3/Y 获取实时遥测数据，在 320x240 TFT 屏幕上渲染 LVGL 仪表界面。

### 功能

- 蓝牙自动扫描、连接、ECDH 密钥认证
- 实时时速、档位、动能回收/能耗可视化
- 车门开关监测（VCSEC 320ms 快速通道）
- 充电状态（功率、SOC、剩余时间）
- 胎压、内外温度、电池续航
- 场景驱动轮询：驾驶 / 开门 / 充电模式自适应
- 首次刷卡配对 + 重启自动重连

### 硬件要求

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| 屏幕 | ILI9341 / ST7789 SPI TFT (320x240) |
| 无线 | BLE 4.2+ Central 角色 |

### 快速开始

**方式一：直接烧录（推荐）**

1. 从 [Releases](https://github.com/elementchen/TESLA_BLE_TFT/releases) 下载 `tesla_ble_dash.bin`
2. 下载 [乐鑫 Flash 下载工具](https://www.espressif.com/zh-hans/support/download/other-tools)
3. 连接 ESP32-S3，芯片选 `ESP32-S3`，按地址加载文件：

| 文件 | 地址 |
|------|------|
| `bootloader.bin` | `0x0000` |
| `partition-table.bin` | `0x8000` |
| `tesla_ble_dash.bin` | `0x20000` |

4. SPI 设置：**80MHz, QIO, 16MB Flash**。点 START 烧录。

**方式二：编译源码**

```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

### 烧录后 — 配置

固件需要你的 **Tesla VIN** 和**显示屏引脚**。使用 USB 配置工具：

| 平台 | 方式 |
|------|------|
| **macOS 图形界面** | 从 Releases 下载 `ESP32_Config_Tool_macOS.zip`，解压双击 |
| **Windows EXE** | 从 Releases 下载 `ESP32_Config_Tool.exe` |
| **命令行** | `pip install pyserial && python3 mac_flash_app/esp32_config.py set-vin 你的17位VIN码` |

内置常见开发板预设（2.8寸 ESP32-S3 ILI9341、ST7789 等）。

### 配对

重启后屏幕显示 "TAP CARD"。将特斯拉钥匙卡放在中控台感应区完成授权。配对后每次开机自动重连。

### 致谢 / Credits

- [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) · LVGL · SquareLine Studio · ESP-IDF

### 许可证 / License

MIT
