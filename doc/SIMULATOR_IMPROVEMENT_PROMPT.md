# Windows 数据模拟器改进 — 完整提示词

> 可直接交付给 Windows 端的 AI Agent 作为独立任务。

---

## 一、背景

你有一个 **Windows Python BLE 模拟器**（`Simulation_app/sim_server.py`），当前它模拟特斯拉车机的 BLE Peripheral，向 ESP32 仪表盘发送遥测数据。

**当前模拟器的问题**：它以理想 20Hz（每 50ms）主动推送数据，零延迟、零丢包。这与真实特斯拉车机的 BLE 行为**完全不同**，导致在模拟器上调试通过的固件，到真车上会出现卡顿、数据延迟、界面死循环等问题。

**你的任务**：改进模拟器，使其更真实地模拟特斯拉车机的 BLE 通信特征。

---

## 二、特斯拉 BLE 协议核心特征

### 2.1 请求-响应模式（不是推送）

真实车机**不会主动推送**数据。协议是严格的问答模式：
1. ESP32 发送加密的 Protobuf 请求 → BLE Write
2. 车机解密、查询 CAN 总线、编码响应 → BLE Notify
3. ESP32 解密响应、提取数据

### 2.2 响应延迟（关键参数）

| 命令域 | 单次响应耗时 | 说明 |
|--------|-----------|------|
| **VCSEC**（车辆安全） | **300-500ms** | SessionInfo 握手、车辆状态查询 |
| **Infotainment**（遥测数据） | **600-1000ms** | DriveState、ChargeState、ClosuresState 等 |

**每个命令独立排队处理**，车机一次只处理一个。如果有 3 个命令排队，最后一个要等 3 × 800ms = 2.4 秒才能拿到响应。

### 2.3 消息格式

```
[2 字节大端长度] [加密的 Protobuf 二进制]
```

- 长度 = Protobuf 部分的字节数（不含这 2 字节）
- 模拟器当前发送的是**未加密的 53 字节 SimPayload**，ESP32 对此有特殊处理（长度判断绕过加密）
- BLE Write 最大 18 字节/包，大消息需要分块

### 2.4 命令队列与拥堵

- 真实车机命令处理速率：**~1.2 条/秒**
- 如果 ESP32 发送速率 > 1.2 条/秒，命令堆积
- 队列深度过大时，新命令被拒绝，导致数据**冻结数十秒**

实车日志中观察到的典型拥堵模式：
```
Command queue at 29/32 - consider throttling polls
[Drive State Poll] Command completed successfully in 863 ms
Command queue at 29/32 - consider throttling polls
...
```

### 2.5 每个响应包含的数据

| 请求类型 | 一个响应包内的字段 |
|---------|------------------|
| **DriveState** | speed_float, speed, shift_state, power, odometer, 导航信息 |
| **ClosuresState** | 6门+2盖 开闭, locked, 车窗, 哨兵, is_user_present |
| **ChargeState** | battery_level, battery_range, charger_power, charge_limit_soc, 充电状态 |
| **ClimateState** | inside_temp, outside_temp, 座椅加热, 风扇, 空调 |
| **TirePressure** | 四轮胎压, 软/硬告警 |

**关键认知**：同一请求类型的所有字段在同一次 BLE 往返中返回。比如 speed + gear + power 都在一个 DriveState 包里，拿 speed 的同时拿 power 不会增加任何开销。

---

## 三、模拟器需要改进的具体内容

### 3.1 当前行为（需要改掉的）

```python
# 当前: 每 50ms 主动推送一次 53 字节 SimPayload
while True:
    payload = pack_telemetry(data)
    server.notify(payload)  # BLE Notification 直接推送
    await asyncio.sleep(0.050)  # 20Hz
```

问题：
- 主动推送 → 真实车机是问答模式
- 20Hz → 真实车机每秒只能处理 ~1.2 个请求
- 无延迟 → 每个命令应该有 300-1000ms 延迟
- 无队列模拟 → 没有命令积压场景
- 固定数据包 → 没有分数据类型的响应

### 3.2 改进目标

#### A. 改为问答模式

模拟器不再是主动推送，而是**等待 ESP32 发送请求 → 模拟车机处理 → 返回响应**。

ESP32 通过 BLE Write Characteristic（UUID `00000212-...`）发送请求。模拟器应监听这个 characteristic 的写入事件，收到请求后才构造并发送响应。

#### B. 按数据类型模拟延迟

不同的请求类型应有不同的响应延迟：

```python
RESPONSE_DELAY = {
    "vcsec":       (300, 500),    # 300-500ms
    "drive_state": (600, 1000),   # 600-1000ms
    "closures":    (600, 1000),
    "charge":      (600, 1000),
    "climate":     (600, 1000),
    "tpms":        (600, 1000),
}
```

每次响应在范围内**随机取值**，模拟真实无线环境的不确定性。还可以加入**偶尔的长尾延迟**（如 5% 的概率延迟 ×2）模拟 BLE 重传。

#### C. 命令队列模拟

维护一个 FIFO 命令队列，深度上限 32。如果 ESP32 发送请求时队列已满（32/32），模拟器应**拒绝该命令**（不回复，或用错误码回复），而不是接受并排队。

队列消化速率：每 ~800ms 处理一个命令。处理流程：
1. 取出队首命令
2. 等待对应的 `RESPONSE_DELAY`
3. 构造响应数据包
4. 通过 BLE Notify 发送
5. 处理下一个

#### D. 加入真实的抖动和不稳定性

```python
import random

# 5% 概率发生额外延迟（模拟 BLE 干扰）
if random.random() < 0.05:
    delay *= 2.0

# 2% 概率丢包（无响应，模拟 BLE 信号弱）
if random.random() < 0.02:
    return  # 不回复，让 ESP32 超时

# 连接断开模拟（可手动触发，用于测试 ESP32 重连逻辑）
```

#### E. 模拟器 GUI 增强建议

在现有 Tkinter 界面中加入：
- **当前命令队列深度** 实时显示
- **最近 10 条命令的响应延迟** 列表
- **模拟丢包率** 滑块（0% - 20%）
- **模拟延迟倍率** 滑块（1x - 5x）
- **一键注入故障** 按钮（如：模拟连接断开 10 秒）

---

## 四、现有代码结构

模拟器位于 `Simulation_app/sim_server.py`，单文件 ~1645 行 Python。

**关键函数/区域**：
- `pack_telemetry()` — 打包 53 字节 SimPayload
- `generate_mock_frame()` — 自动脚本模式的数据生成（9 阶段驾驶循环）
- BLE 服务初始化 — 创建 Service UUID `00000211-...` + Write/Notify characteristic
- Tkinter GUI — 手动控制模式，滑块调节各项数据
- 主循环 @20Hz — `asyncio.sleep(0.050)` + `server.notify()`

**依赖**：
- `bless`（Windows WinRT BLE API）— 默认 BLE 后端
- `bumble`（USB HCI dongle）— 可选跨平台后端（`--bumble` 参数）

**ESP32 侧的特殊处理**：
ESP32 固件 `main/ble_adapter.cpp` 中有一个 53 字节长度检测：如果收到的 BLE Notification 恰好是 53 字节（`sizeof(SimPayload)`），直接当模拟数据解析，跳过加密解密。如果改动数据格式，需要同步修改 ESP32 端。

---

## 五、建议的开发步骤

### Phase 1：问答模式 + 单命令延迟（最小可行改动）

- [ ] 监听 BLE Write Characteristic，收到数据后触发响应
- [ ] 停止主动推送循环
- [ ] 收到请求后，等待随机延迟（600-1000ms），然后通过 Notify 发回 53 字节 SimPayload
- [ ] 验证 ESP32 能正常接收并显示数据

### Phase 2：命令队列 + 拥堵模拟

- [ ] 实现 FIFO 队列（深度 32）
- [ ] 队列满时拒绝新命令
- [ ] ESP32 连续发送多个请求时，按队列顺序逐一回复

### Phase 3：抖动 + GUI 增强

- [ ] 加入随机延迟抖动、丢包概率
- [ ] GUI 显示队列深度、延迟列表
- [ ] 故障注入按钮

### Phase 4：多数据类型支持（可选）

- [ ] 区分不同的请求类型（解析 Protobuf 或通过长度/特征区分）
- [ ] 不同类型使用不同的延迟范围（DriveState 600-1000ms vs VCSEC 300-500ms）

---

## 六、参考资源

| 资源 | 路径/URL |
|------|---------|
| 模拟器源码 | `Simulation_app/sim_server.py` |
| ESP32 BLE 适配器 | `main/ble_adapter.cpp` |
| SimPayload 结构 | `main/sim_payload.h` |
| Tesla BLE 协议库 | `components/tesla-ble/` |
| 数据参考手册 | `doc/tesla_ble_data_reference.md` |
| VCSEC 协议参考 | `doc/vcsec_proto_reference.md` |
| 实车拥堵日志 | `doc/WCH.txt` |
| 数据抓取讨论 | `doc/数据抓取工具的建议.txt` |
| yoziru/tesla-ble 仓库 | https://github.com/yoziru/tesla-ble |
