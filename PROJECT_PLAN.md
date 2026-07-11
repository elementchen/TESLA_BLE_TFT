# Tesla BLE Dashboard — ESP32-S3 Implementation Plan

## Context

构建一个基于 ESP32-S3 + 0.96" SPI OLED (128x64) 的 Tesla 车载仪表盘，通过 BLE 直连特斯拉车辆，实时显示时速、档位(P/R/N/D)、里程。行车时持续刷新。

核心依赖：
- [yoziru/tesla-ble](https://github.com/yoziru/tesla-ble) v5.1.1 — C++ 库，实现完整 Tesla BLE 协议（ECDH + AES-GCM + Protobuf）
- [nopnop2002/esp-idf-ssd1306](https://github.com/nopnop2002/esp-idf-ssd1306) — SSD1306/SH1106 OLED 驱动，支持 SPI 模式
- ESP-IDF v5.x + NimBLE

Tesla BLE 协议要点：
- BLE Service UUID: `00000211-b2d1-43f0-9b88-960cebf8b91e`
- 两个域：VCSEC（车锁/唤醒/基本状态）和 Infotainment（驾驶数据/电池/空调）
- 时速来源：`DriveState.speed_float`，通过 Infotainment 域 `GetDriveState` 请求获取
- 认证：ECDH (secp256r1) 密钥协商 + AES-128-GCM 加密 + session counter 防重放
- 配对：需用 NFC 钥匙卡在中央扶手确认

---

## Implementation Steps

### Step 1: 创建 ESP-IDF 项目骨架

**文件：** `CMakeLists.txt` (root), `main/CMakeLists.txt`, `idf_component.yml`, `sdkconfig.defaults`

创建标准 ESP-IDF v5.x 项目结构，设置目标芯片为 `esp32s3`。

`idf_component.yml` 声明依赖：
```yaml
dependencies:
  yoziru/tesla-ble:
    git: https://github.com/yoziru/tesla-ble.git
    version: v5.1.1
  nopnop2002/ssd1306:
    git: https://github.com/nopnop2002/esp-idf-ssd1306.git
  espressif/ssd1306:
    version: "^1.0.5"
```

`sdkconfig.defaults` 关键配置：
- NimBLE 启用（替代 Bluedroid，节省 flash/RAM）
- SPI 外设启用
- NVS 启用
- main task stack size 增大（BLE + Tesla 协议栈需要）
- FreeRTOS tick rate 1000Hz
- 启用 C++ exceptions（tesla-ble 库需要）

### Step 2: 实现 BleAdapter — ESP-IDF NimBLE GATT 客户端

**文件：** `main/ble_adapter.h`, `main/ble_adapter.cpp`

继承 `TeslaBLE::BleAdapter` 抽象接口，实现以下方法：

| 方法 | 实现方式 |
|------|---------|
| `connect(address)` | NimBLE `ble_gap_connect()` + 服务发现（按 UUID 查找 Tesla service + read/write characteristics） |
| `disconnect()` | `ble_gap_terminate()` |
| `write(data)` | 分片为 ≤18 字节块（BLE MTU 限制），通过 `ble_gattc_write_no_rsp()` 发送到 write characteristic `00000212-...` |
| 通知接收 | 注册 `00000213-...` characteristic 的 GATT 通知，收到数据后调用 `vehicle->on_rx_data(data)` 送入协议栈 |

参考 `yoziru/esphome-tesla-ble` 的 `BleAdapterImpl`。

**关键实现细节：**
- BLE 扫描：按 `S + <16 hex chars> + C` 格式过滤设备名，根据 VIN 的 SHA1 前8字节验证
- 消息分片：2字节大端长度前缀 + Protobuf payload，按 18 字节分块
- 接收重组：累积通知数据，按长度前缀重组完整消息后送入 `Vehicle::on_rx_data()`
- 写队列：参考 ESPHome 的 `BLETXChunk` 队列，每次 loop 发送一个分片

### Step 3: 实现 StorageAdapter — NVS 持久化

**文件：** `main/storage_adapter.h`, `main/storage_adapter.cpp`

继承 `TeslaBLE::StorageAdapter`，实现 `load(key)`, `save(key, data)`, `remove(key)`。

- NVS namespace: `"tesla_ble"`
- 存储内容：私钥 (`private_key`)、VCSEC session (`tk_vcsec`)、Infotainment session (`tk_infotainment`)

### Step 4: 实现 OLED 显示模块

**文件：** `main/display.h`, `main/display.cpp`

封装 `nopnop2002/esp-idf-ssd1306`，针对 128x64 分辨率设计仪表盘布局：

```
┌──────────────────────────────┐
│  [D]    88 km/h              │  ← 档位 + 大字号时速
│                              │
│         里程: 42,350 km       │  ← 底部小字里程
└──────────────────────────────┘
```

- 大号字体显示时速（使用 SSD1306 内置 5x7 或 u8g2 字体缩放）
- 档位指示器：P/R/N/D 加边框高亮
- 里程显示
- 可选：电池电量百分比图标

**OLED 引脚配置（SPI 4线）：**
- MOSI: GPIO 11
- SCLK: GPIO 12
- CS: GPIO 10
- DC: GPIO 13
- RESET: GPIO 14
（通过 `menuconfig` 可配置）

### Step 5: 实现主应用程序

**文件：** `main/main.cpp`

主逻辑流程：

1. **初始化阶段**
   - NVS flash 初始化
   - SPI 总线 + OLED 初始化，显示启动画面 "Tesla BLE Dash"
   - 创建 `StorageAdapterImpl` + `BleAdapterImpl`
   - 加载 VIN（从 NVS 或编译时配置）
   - 创建 `TeslaBLE::Vehicle` 实例

2. **配对模式（首次使用）**
   - 检查 NVS 中是否已有私钥
   - 若无：OLED 显示 "Pairing: Tap NFC Card"
   - 调用 `vehicle->pair()` 发起 whitelist 流程
   - 用户在中控台刷 NFC 钥匙卡确认
   - 配对成功后保存 session 到 NVS

3. **连接 + 行驶仪表模式**
   - BLE 扫描并连接车辆
   - 注册 `set_drive_state_callback` — 收到时速/档位/里程数据后更新 OLED
   - 主循环：`vehicle->loop()` + `ble_adapter->process_write_queue()`
   - 每 500ms 调用 `vehicle->drive_state_poll(WakePolicy::NO_WAKE_SKIP)`
   - 如果车辆已唤醒（行车中），持续轮询
   - 如果车辆休眠（停车），降低轮询频率或暂停

4. **主循环结构**
   ```
   while (1) {
       vehicle->loop();                    // Tesla 协议状态机
       ble_adapter->process_write_queue(); // 发送 BLE 分片
       
       if (is_connected && now - last_poll > POLL_INTERVAL_MS) {
           vehicle->drive_state_poll(WakePolicy::NO_WAKE_SKIP);
           last_poll = now;
       }
       
       display->render(speed, gear, odometer); // 刷新屏幕
       vTaskDelay(pdMS_TO_TICKS(10));     // ~100Hz 主循环
   }
   ```

### Step 6: 数据结构定义

**文件：** `main/dash_data.h`

定义从 `CarServer_DriveState` Protobuf 中提取的仪表数据：

```cpp
struct DashData {
    float speed_kmh = 0;      // DriveState.speed_float (转换为 km/h)
    char gear = 'P';          // DriveState.shift_state → 'P'/'R'/'N'/'D'
    uint32_t odometer_km = 0; // DriveState.odometer_in_hundredths_of_a_mile → 转换为 km
    bool valid = false;       // 数据是否有效
};
```

直接从 `CarServer_DriveState` proto 结构体提取并转换。

### Step 7: 构建与烧录脚本

`CMakeLists.txt` 配置目标为 `esp32s3`，启用 C++17。

烧录命令：
```bash
idf.py set-target esp32s3
idf.py menuconfig  # 配置 OLED 引脚、VIN
idf.py build flash monitor
```

---

## Files to Create

| 文件 | 用途 |
|------|------|
| `CMakeLists.txt` | 根 CMake 配置 |
| `idf_component.yml` | ESP-IDF 组件依赖 |
| `sdkconfig.defaults` | 默认 Kconfig 值 |
| `main/CMakeLists.txt` | 主程序 CMake |
| `main/main.cpp` | 主入口 + 应用逻辑 (~400行) |
| `main/ble_adapter.h` | BLE 适配器头文件 |
| `main/ble_adapter.cpp` | NimBLE GATT 客户端实现 (~500行) |
| `main/storage_adapter.h` | NVS 存储适配器头文件 |
| `main/storage_adapter.cpp` | NVS 读写实现 (~100行) |
| `main/display.h` | OLED 显示模块头文件 |
| `main/display.cpp` | SSD1306 SPI 仪表盘渲染 (~200行) |
| `main/dash_data.h` | 仪表数据结构 + 单位转换 (~50行) |

**总预估代码量：约 1400 行**

---

## Verification

1. **编译验证**：`idf.py build` 通过，无错误无警告
2. **OLED 验证**：上电后显示 "Tesla BLE Dash" 启动画面
3. **BLE 扫描验证**：串口日志输出发现的 Tesla 车辆 BLE 设备名
4. **配对验证**：首次使用，OLED 显示 "Tap NFC Card"，刷 NFC 卡后确认配对成功
5. **数据轮询验证**：连接成功后，串口日志输出 speed/gear/odometer 原始值
6. **显示验证**：OLED 实时更新时速、档位、里程
7. **重连验证**：断电重启后能从 NVS 加载 session，无需重新配对

**调试技巧：**
- 使用 `idf.py monitor` 查看串口日志（Tesla BLE 协议层日志由 `yoziru/tesla-ble` 库输出）
- 可先用 `nopnop2002/esp-idf-ssd1306` 的 TextDemo 验证 OLED 硬件接线
- 配对步骤建议先在停车状态下完成，确保车辆唤醒且蓝牙稳定
