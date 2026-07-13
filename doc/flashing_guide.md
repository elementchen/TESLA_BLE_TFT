# 软件烧录说明文档 (Software Flashing Guide)

本项目的固件烧录支持两种方式：针对开发者的 **命令行一键自动烧录 (flash.bat)**，以及针对普通用户的 **乐鑫官方图形化工具烧录 (ESP Flash Download Tool)**。

---

## 方式一：命令行一键编译并烧录（推荐开发者）

在您的电脑上已经安装好 ESP-IDF v5.x 环境的前提下，您可以通过根目录下的批处理脚本快速烧录：

1. 将 ESP32-S3 开发板通过 USB 数据线连接到电脑。
2. 确认设备的 COM 口号（例如 `COM7`）。如果 COM 口发生变化，请用文本编辑器打开根目录下的 [flash.bat](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/flash.bat)，将末尾的 `-p COM7` 修改为您的实际端口号。
3. 双击运行根目录下的 [flash.bat](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/flash.bat) 即可自动开始编译、链接并烧录固件。

---

## 方式二：使用官方图形化工具烧录（推荐普通用户）

如果您没有安装 ESP-IDF 命令行环境，可以直接使用乐鑫官方提供的 **ESP Flash Download Tool** 烧录 `release` 目录中的打包 bin 文件。

### 1. 烧录工具下载
* 请访问乐鑫官方支持页面下载最新版的 [Flash 下载工具](https://www.espressif.com.cn/zh-hans/support/download/other-tools)。

### 2. 软件运行与芯片选型
双击打开工具，在弹出的窗口中选择：
* **Chip Type**: `ESP32-S3`
* **WorkMode**: `Develop` 或 `Factory`
* **LoadMode**: `USB` (如果是串口连接，选择 `UART`)

### 3. 固件文件与烧录地址配置
在软件的配置界面上方，勾选并添加 `release/v1.0.0` 目录下的三个二进制文件，并**严格填写其对应的 Flash 偏移地址**：

| 固件二进制文件 | 对应的烧录地址 (Offset) | 备注描述 |
| :--- | :---: | :--- |
| **`bootloader.bin`** | **`0x0`** | 二级引导装载程序 |
| **`partition-table.bin`** | **`0x8000`** | 自定义分区表文件 |
| **`tesla_ble_dash.bin`** | **`0x20000`** | 仪表盘主程序固件 |

> [!IMPORTANT]
> 烧录地址（右侧的文本框）必须精确填写。如果填错，设备上电将无法启动并一直闪红灯。

### 4. 烧录物理参数配置
在软件的右下角，配置如下芯片烧录参数：
* **SPI SPEED**: `80MHz`
* **SPI MODE**: `DIO`
* **FLASH SIZE**: `16MB` (或者选择 `128Mb`)
* **COM**: 选择您开发板对应的 COM 端口
* **BAUD**: 推荐选择 `460800` 或 `921600` (高波特率速度更快)

### 5. 开始烧录
1. 确认上述配置无误后，点击软件左下角的 **START** 按钮。
2. 软件会显示 `CONNECT`，并开始写入 Flash（进度条开始走动）。
3. 烧录完成后，进度条上方会提示 **FINISH**。此时您可以拔下 USB 数据线，固件已安全写入开发板。
