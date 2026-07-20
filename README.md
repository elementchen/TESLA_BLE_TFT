# Tesla BLE TFT Dashboard (ESP32-S3) 🚗⚡

一个基于 **ESP32-S3** 与 **LVGL 8** 的开源特斯拉仪表盘系统。项目使用低功耗蓝牙 (BLE) 协议与真实特斯拉车机（Model 3 / Model Y 等）建立加密连接，或配合配套的桌面仿真器进行全真车机遥测数据驱动渲染。

---

## 🌟 核心特性

* **真车 BLE 无线通信**：内置官方规范的 `tesla-ble` 协议库，支持基于 17位 VIN 的安全寻址、卡片刷卡授权配对、ECDH 共享密钥协商与 VCSEC/CarServer 加密遥测传输。
* **高鲁棒性防死锁状态机**：
  * 主循环采用物理层（`ble_adapter->process()`）与协议业务层（`vehicle->loop()`）双引擎轮询驱动。
  * 内置 Flash NVS 秘钥失效与 Session 拒绝超时自动擦除机制，自动恢复刷卡重配状态。
* **炫酷 HMI UI 界面**：
  * 精准呈现时速 (KM/H)、能量回收与输出功率进度条、实时档位 (P/R/N/D/SNA)。
  * 车身 6 门开启状态高亮、电池 SOC 电量、续航里程、内外环境温度与四轮胎压监测 (TPMS)。
  * 充电波纹动画与超级充电/慢充功率动态渲染。
* **跨平台桌面仿真套件支持**：
  * 根目录下包含独立的 Windows 仿真端子项目 `Simulation_app`，支持一键拉起 GUI 控制面板，在电脑上无线模拟车机广播进行全功能 UI 测试。

---

## 🛠️ 硬件与开发环境

* **主控芯片**：ESP32-S3 (推荐 8MB / 16MB Flash, 支持 PSRAM)
* **显示屏**：TFT LCD (分辨率根据 GUI 布局适配，常用 ST7789 / GC9A01 等)
* **软件框架**：ESP-IDF v5.x + LVGL 8.x + NimBLE

---

## 🚀 编译与烧录指南

### 1. 克隆本仓库
```bash
git clone https://github.com/elementchen/TESLA_BLE_TFT.git
cd TESLA_BLE_TFT
```

### 2. 在 Windows 上编译与烧录
环境配置完成后，直接运行根目录下的自动化批处理：
```powershell
.\flash.bat
```

### 3. 在 macOS / Linux 上编译与烧录
设置 ESP-IDF 环境后，使用标准命令行编译：
```bash
# 查看连接的 ESP32 串口设备
ls /dev/tty.usb*

# 编译、烧录并开启串口实时日志监视
idf.py -p /dev/tty.usbmodem14101 flash monitor
```

---

## 📂 项目目录结构

```text
TESLA_BLE_TFT/
├── CMakeLists.txt              # CMake 构建入口
├── flash.bat                   # Windows 一键编译烧录脚本
├── README.md                   # 本说明文档
├── doc/                        # 详细架构与跨设备接续开发指南
│   └── CROSS_DEVICE_DEV_GUIDE.md # 跨平台(Windows <-> Mac)接续开发与实车排查手册
├── main/                       # ESP32 固件主源码
│   ├── main.cpp                # 固件入口与 HMI 主状态机
│   ├── ble_adapter.cpp/.h      # NimBLE 蓝牙适配层
│   ├── storage_adapter.cpp/.h  # Flash NVS 秘钥存储适配层
│   └── ui/                     # LVGL GUI 界面组件与 Font/Image 资源
├── components/                 # 依赖组件库
│   └── tesla-ble/              # 特斯拉 BLE 官方协议与 Protobuf 解析库
└── Simulation_app/             # 独立的 Python 桌面端车机 BLE 仿真器 (包含独立 README & GUI)
```

---

## 📜 许可与致谢

* 协议库参考并基于特斯拉官方 [vehicle-command](github.com/teslamotors/vehicle-command) BLE 规范设计。
* 欢迎提交 PR 与 Issue 一起完善项目！
