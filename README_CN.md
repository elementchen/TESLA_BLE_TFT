# Tesla BLE TFT 仪表盘

基于 ESP32-S3 的开源特斯拉数字仪表盘。通过 BLE 连接 Tesla Model 3/Y 获取实时遥测数据，在 320x240 TFT 屏幕上渲染 LVGL 仪表界面。

[English](README.md)

## 功能

- 蓝牙自动扫描、连接、ECDH 密钥认证
- 实时时速、档位、动能回收/能耗可视化
- 车门开关监测（VCSEC 320ms 快速通道）
- 充电状态（功率、SOC、剩余时间）
- 胎压、内外温度、电池续航
- 场景驱动轮询：驾驶 / 开门 / 充电模式自适应
- 首次刷卡配对 + 重启自动重连

## 硬件要求

![开发板](doc/IMG_7255.jpg)

| 组件 | 型号 |
|------|------|
| 主控 | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| 屏幕 | ILI9341 / ST7789 SPI TFT (320x240) |
| 无线 | BLE 4.2+ Central 角色 |

## 快速开始

### 方式一：直接烧录（推荐）

1. 从 [Releases](https://github.com/elementchen/TESLA_BLE_TFT/releases) 下载 `tesla_ble_dash.bin`
2. 下载 [乐鑫 Flash 下载工具](https://www.espressif.com/zh-hans/support/download/other-tools)
3. 连接 ESP32-S3，芯片选 `ESP32-S3`，按以下地址加载文件：

| 文件 | 地址 |
|------|------|
| `bootloader.bin` | `0x0000` |
| `partition-table.bin` | `0x8000` |
| `tesla_ble_dash.bin` | `0x20000` |

4. SPI 设置：**80MHz, QIO, 16MB Flash**。点 START 烧录。

### 方式二：编译源码

需要 ESP-IDF v5.5.4。

```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

## 烧录后 — 必须配置

固件需要你的 **Tesla VIN** 和**显示屏引脚**才能工作。使用 USB 配置工具写入：

**macOS 图形界面：**
```bash
pip3 install pyserial
python3 mac_flash_app/esp32_config_gui.py
```

**macOS / Windows 命令行：**
```bash
pip install pyserial
python3 mac_flash_app/esp32_config.py set-vin 你的17位VIN码
python3 mac_flash_app/esp32_config.py save reboot
```

工具内置常见开发板预设（2.8寸 ESP32-S3 ILI9341、ST7789 等）。如果你的板子引脚不同，在 GUI 中修改 7 个 SPI 引脚值即可。

## 配对

重启后屏幕显示 "TAP CARD"。将特斯拉钥匙卡放在中控台感应区完成授权。配对后每次开机自动重连。

## 致谢

- **Tesla BLE 协议**: [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) v5.1.1
- **LVGL**: 嵌入式 GUI 框架 v8.4
- **SquareLine Studio**: UI 设计工具
- **ESP-IDF**: 乐鑫物联网开发框架

## 许可证

MIT
