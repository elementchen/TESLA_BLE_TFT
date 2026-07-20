# 特斯拉 BLE TFT 仪表盘 - 跨设备与多 Agent 接续开发指南 💻➡️🍎

本文档旨在为开发者在 **Windows 台式开发机** 与 **Mac 笔记本（车上实车调试）** 之间切换、以及在不同 AI Agent / IDE 工具间接续项目开发时提供全面的技术要点与指引。

---

## 🧭 1. 快速同步与接续开发流程

### 🔄 A. 代码同步规范 (Git 流)

项目配置了完整的 `.gitignore`，自动过滤了平台特定的 `build/`、`sdkconfig` 与 `Simulation_app/` 离线中间件。

1. **在台式机（Windows）上完成修改后推送**：
   ```powershell
   git add .
   git commit -m "Commit message"
   git push origin main
   ```
2. **在 Mac 笔记本（车上实车测试）上拉取最新代码**：
   ```bash
   git pull origin main
   ```
3. **在 Mac 上测试修改后提交**：
   ```bash
   git commit -am "Fix: 实车蓝牙握手细节调整"
   git push origin main
   ```
4. **回到台式机后拉取**：
   ```powershell
   git pull origin main
   ```

---

## 🛠️ 2. Mac 笔记本车上测试实操指南

### 🔌 A. 串口设备识别与烧录
在 macOS 下，ESP32-S3 的 USB 串口设备通常识别为 `/dev/tty.usbmodem*`。

1. 查看 Mac 连接的 ESP32 串口：
   ```bash
   ls /dev/tty.usb*
   ```
2. **三合一极速命令 (编译 + 烧录 + 开启实时日志监视)**：
   ```bash
   idf.py -p /dev/tty.usbmodem14101 flash monitor
   ```
   *(注：在 monitor 日志界面中，按 `Ctrl + ]` 随时退出输出)*。

---

## 🧠 3. 固件架构核心要点 (写给接手的 Agent/开发者)

当您（或新的 AI Agent）在 Mac 笔记本或新对话中接手本项目时，请务必掌握以下核心架构设计：

### 🔑 Key Point 1: 双驱动轮询主循环 (`main.cpp`)
主循环中必须同时保持对物理适配器与协议栈状态机的双驱动轮询：
```cpp
while (true) {
    // 1. 驱动底层 BLE 适配器 (处理 NimBLE 延迟服务探索与数据包)
    if (ble_adapter) ble_adapter->process();

    // 2. 驱动特斯拉实车协议栈 (处理 VCSEC/CarServer 密文 Session 握手与命令队列)
    if (vehicle) vehicle->loop();

    // 3. 遥测数据更新与 LVGL 屏幕渲染...
}
```
* **注意**：遗漏 `ble_adapter->process()` 会导致卡在 `DISCOVER` 状态；遗漏 `vehicle->loop()` 会导致实车 Session 同步卡死在转圈界面。

### 🔑 Key Point 2: 基于 VIN 的蓝牙广播过滤机制
* 车机蓝牙设备名基于车辆 17位 VIN 的 SHA1 计算：`Sa7785a96101d0c3fC`。
* `ble_adapter.cpp` 严格基于基于设备名匹配，**绝不引入基于 Service UUID 的模糊匹配**，以防在复杂停车场多车并发场景下误连邻车。

### 🔑 Key Point 3: 自动擦除废旧秘钥防死锁机制
* 当车机端删除了该 ESP32 钥匙时，ESP32 Flash 中仍留有旧 `private_key`。
* `main.cpp` 内置了同步超时判定：连上车机后若连续 8 秒无法完成 Session 握手，将**自动擦除 Flash 中的废旧密钥**（`storage_adapter->remove("private_key")`），并自动切入刷卡授权界面（`TAP KEYCARD ON CENTER CONSOLE`）。

---

## 📝 4. 实车日志关键排查线索

车上调试时，观察 `idf.py monitor` 串口日志中的关键 Tag：

| 日志 Tag | 正常标志 | 异常诊断 |
| :--- | :--- | :--- |
| `BleAdapter` | `*** MATCH! Connecting: Sa77... ***` | 未找到设备：检查 VIN 是否配置正确或蓝牙天线 |
| `BleAdapter` | `MTU exchanged: conn=1 mtu=256` | 停在 MTU：检查 `ble_adapter->process()` 是否在轮询 |
| `TeslaBLE` | `Session state -> READY` | 无限转圈：检测 `vehicle->loop()` 或检查 Flash 密钥擦除机制 |
| `TeslaDash` | `on_drive_state: speed=XX gear=D` | 正常接收遥测：UI 界面滑入 `ui_Drive` |
