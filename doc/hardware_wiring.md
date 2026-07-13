# 硬件接线说明文档 (Hardware Wiring Guide)

本项目基于 **ESP32-S3-N8R16** 核心 MCU 开发，适配 **240x296 分辨率的 ST7789 SPI 彩色液晶屏** (竖屏，无旋转模式)。

---

## 📌 液晶屏接口引脚接线定义

液晶屏与 ESP32-S3 的物理连线对应关系如下，请严格按照下表进行硬件焊接或杜邦线连接：

| 屏幕引脚名称 | 功能描述 | ESP32-S3 对应 GPIO 编号 | 备注说明 |
| :---: | :--- | :---: | :--- |
| **GND** | 电源地线 | **GND** | 共同接地 |
| **3V3** | 电源正极 (3.3V) | **3V3 / VCC** | 供电电源 |
| **SCL / SCK** | SPI 时钟信号线 | **GPIO 21** | SPI Clock |
| **SDA / MOSI**| SPI 数据信号线 | **GPIO 47** | SPI Master Out Slave In |
| **RES / RST** | 屏幕硬件复位脚 | **GPIO 45** | LCD Hardware Reset |
| **DC** | 数据/命令选择脚 | **GPIO 40** | Data/Command Selection |
| **CS** | SPI 片选信号脚 | **GPIO 41** | SPI Chip Select |
| **BLK / LED** | 屏幕背光控制引脚 | **GPIO 42** | Backlight Control (高电平开启) |

---

## ⚠️ 硬件接线注意事项

1. **背光引脚 (BLK/LED)**：
   * 本固件已在初始化阶段将 **GPIO 42** 配置为输出并强拉为**高电平 (3.3V)**。
   * 上电后，屏幕背光会自动亮起。无需额外外接 VCC 供电控制背光。
2. **复位引脚 (RES/RST)**：
   * 使用 **GPIO 45** 作为复位脚。由于 GPIO 45 是 ESP32-S3 芯片的 Strapping Pin 之一，本固件已在软件层面避开了所有与之冲突的 I2C/SPI 时序，确保系统复位或软重启时不会导致芯片电压供电异常（VDD_SPI 保持 3.3V）。
3. **共地连接 (GND)**：
   * 确保液晶屏的 GND 与 ESP32-S3 核心板的 GND 牢固连接，避免因地线参考电压飘移导致 SPI 高频通信（80MHz）出现花屏或数据丢包。
