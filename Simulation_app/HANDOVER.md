# Tesla BLE TFT HMI Windows Simulator - Handover Guide (项目移交与后续开发指南)

致接手本项目的 AI Agent 伙伴：

欢迎接手 **Tesla BLE TFT 仪表盘 - Windows 仿真端 (Simulation_app)** 子项目！本项目的目标是为 ESP32 车规级仪表盘提供一个 100% 模拟真实特斯拉车机的低功耗蓝牙 (BLE) 数据灌入源，免去连接真实车辆的门槛，以 20Hz 刷新率无线驱动仪表的 HMI 动画与逻辑状态机。

本指南将为您剖析整个项目的架构、数据流向、协议细节，并为您指出后续的开发路线图。

---

## 🧭 项目架构与数据流向

本模拟器位于 Windows 主机侧，扮演低功耗蓝牙 (BLE) 外设（Peripheral）角色，与扮演中心（Central）角色的 ESP32 进行无线通信：

```mermaid
graph LR
    subgraph Windows Host (Simulation_app)
        A[sim_server.py] -->|1. Generate Telemetry| B[Generate Mock Data Script]
        B -->|2. Pack to SimPayload| C[struct.pack format]
        C -->|3. BLE Notify| D[bless BLE Server]
    end
    D == Wireless BLE ==> E[ESP32 NimBLE Central]
    subgraph ESP32 (Firmware Side)
        E -->|4. Intercept 53-byte Pack| F[ble_adapter.cpp]
        F -->|5. Bypass Decryption| G[main.cpp Loop]
        G -->|6. Render HMI| H[LVGL Drive/Charge UI]
    end
```

### 🗝️ 为什么不走实车的 ECDH 和 AES-GCM 加密？
真实的特斯拉车机通信包含复杂的私钥加载、ECDH（X25519）共享密钥协商以及 AES-GCM-128 加密认证。
为了在 Windows 上免去数千行复杂加密数学算法的编写，本系统在 ESP32 适配层 [ble_adapter.cpp](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/main/ble_adapter.cpp#L388-L425) 植入了**自适应长度旁路劫持**：
* 当接收到的蓝牙数据包长度**正好为 53 字节**时，ESP32 判定其为本仿真器发出的裸包（`SimPayload`），直接解包渲染，**跳过所有解密握手**！这极大地方便了 HMI 界面的无线开发与参数调试。

---

## 📡 BLE 通信与协议规范

### 1. 广播服务与特征值定义
模拟器在空中广播以下特斯拉特定 UUID 属性：
* **Service UUID**: `00000211-b2d1-43f0-9b88-960cebf8b91e`
* **Write Characteristic UUID** (用于接收 ESP32 命令，可选): `00000212-b2d1-43f0-9b88-960cebf8b91e` (Permissions: Writeable)
* **Notify Characteristic UUID** (数据发送主通道): `00000213-b2d1-43f0-9b88-960cebf8b91e` (Properties: Notify)

### 2. 应用层数据封包：`SimPayload`
数据包为紧凑的二进制对齐结构体，总长度为 **53 字节**。
在 Python 侧通过 `struct.pack(PAYLOAD_FORMAT, ...)` 打包，格式字符串为：
```python
PAYLOAD_FORMAT = "<ffifc6BBfff4f"
```

字段在内存中的排布必须与 C++ 的 [sim_payload.h](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/main/sim_payload.h) 结构体严格保持一致：

| 偏移量 | 字段名称 | 类型 (Python -> C++) | 作用描述 |
| :--- | :--- | :--- | :--- |
| `0 - 3` | `speed_kmh` | `float` -> `float` | 实时车速数字 (KM/H) |
| `4 - 7` | `motor_power_kw` | `float` -> `float` | 电机输出功率 (负数代表能量回收进度条高亮) |
| `8 - 11` | `battery_level` | `int` -> `int32_t` | 剩余电量 SOC (%) |
| `12 - 15` | `battery_range_km` | `float` -> `float` | 估计续航距离 (KM) |
| `16` | `gear` | `char` -> `char` | 当前档位 (`P`/`R`/`N`/`D`/`?`巡航) |
| `17 - 22` | `doors[6]` | `6B` -> `uint8_t[6]` | 车身开口状态 (fl, fr, rl, rr, frunk, trunk) |
| `23` | `locked` | `B` -> `uint8_t` | 中控锁定状态 (0解锁, 1锁定) |
| `24` | `charging` | `B` -> `uint8_t` | 充电枪连接状态 (0未充电, 1充电中) |
| `25 - 28` | `charge_power_kw` | `float` -> `float` | 充电实时功率 (kW) |
| `29 - 32` | `inside_temp` | `float` -> `float` | 车内空气温度 (℃) |
| `33 - 36` | `outside_temp` | `float` -> `float` | 车外环境温度 (℃) |
| `37 - 52` | `tpms[4]` | `4f` -> `float[4]` | 四轮实时胎压 (fl, fr, rl, rr) |

---

## 📂 代码库文件清单

您接手后，Simulation_app 文件夹中的核心文件及其职责如下：
* **[sim_server.py](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/Simulation_app/sim_server.py)**：仿真核心脚本。定义了 `SimPayload` 的打包过程、20Hz 主发送轮询任务，以及内置的 9 阶段遥测变化剧本（实现速度、电量、胎压、充电波纹的动态演进）。
* **[requirements.txt](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/Simulation_app/requirements.txt)**：声明了 `bless` 蓝牙仿真框架依赖。本环境已经安装配置完毕。
* **[run.bat](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/Simulation_app/run.bat)**：开箱即用批处理。
* **[README.md](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/Simulation_app/README.md)**：供普通用户（人类）参考的使用手册。

---

## 🛠️ 后续开发路线图建议 (Roadmap)

当您（新 Agent）在此子项目下进行独立开发时，我们建议您优先探索以下功能：

### 🎯 任务一：编写 GUI 可视化控制面板 (极力推荐)
* **目标**：当前的 `sim_server.py` 是根据内置剧本自动循环运行数据的。这无法做到手动、即时地干预参数。
* **开发策略**：
  * 使用 Python 内置的 `tkinter` 或者 `PyQt`/`PySide` 库，在 `Simulation_app` 中新建一个 **GUI 控制面板**。
  * 提供时速滑块 (Slider)、电量滑块、档位单选框 (Radio Buttons)、车门复选框 (Checkboxes) 以及充电模式开关。
  * 当用户拖动滑块时，GUI 线程实时修改共享内存，BLE 线程以 20Hz 频率实时将这些手动修改打包发送给 ESP32！
  * **效果**：用户在电脑上拖动滑块，ESP32 屏幕上的指针和状态就会瞬间跟随拖动发生改变，极大提升 HMI 联调体验！

### 🎯 任务二：扩展车机状态位
* **目标**：随着 ESP32 仪表盘加入更多 UI 功能（如转向灯指示、远光灯图标、AP辅助驾驶警告、导航方向箭头等），需要扩展通信报文。
* **开发策略**：
  * 在 [sim_payload.h](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/main/sim_payload.h) 尾部增加相应的数据字段（如 `uint8_t turn_signal` 等）；
  * 同步修改 Python 侧 `PAYLOAD_FORMAT` 格式串，确保双向对齐；
  * 在剧本中加入对应字段的动态演进逻辑。

### 🎯 任务三：优化 BLE 断线自动重广播
* **目标**：在某些 Windows 设备上，ESP32 仪表掉电重启后，Windows BLE 外设广播可能发生假死，需要手动重启脚本。
* **开发策略**：
  * 在 `sim_server.py` 中，强化 bless 的连接断开事件监听。一旦感知到客户端连接丢失，自动调用 `server.stop()` 并在 1 秒后重新拉起 `server.start()` 发起广播，达到极高水准的“无线重连鲁棒性”。

祝您接后续的开发顺利！让我们的仿真器变得更加强大！
