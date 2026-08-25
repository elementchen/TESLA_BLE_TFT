# BLE 轮询调度逻辑

> **约定**：每次修改轮询调度（`main/main.cpp` 的 `register_poll` / 档位联动、`main/poll_scheduler.cpp` 的冷却与调度算法）后，**必须同步更新本文档**，并在此处记录变更日志。这样我们才能根据实际调试结果精准沟通、持续优化。

## 1. 总体架构

特斯拉车机（Model 3/Y）大约每 ~800ms 消化一条 BLE 命令。为保持命令队列浅、响应延迟低，我们用一个**场景驱动的轮询调度器** `PollScheduler`，每轮最多派发 **1 个** poll。

- 场景（`DashMode`）：`CONNECTING` / `DRIVING` / `DOOR_OPEN` / `CHARGING`。
- 两个数据域：
  - **VCSEC**（~320ms 响应）：车锁/车门/唤醒/配对。
  - **Infotainment**（~800ms 响应）：DriveState / ClosuresState / ChargeState / ClimateState / TirePressureState。

## 2. 场景切换（`main.cpp` 主循环）

```
session_established 后：
  任一车门开            → DOOR_OPEN
  充电中 (charging)     → CHARGING
  否则                  → DRIVING
未建立会话              → CONNECTING
```

模式切换调用 `scheduler.set_mode(mode)`，它按各 slot 在该场景的 interval 启用/禁用（0 = 禁用）。

## 3. 注册表（`main.cpp::init_tesla_ble` 里的 `register_poll`）

`register_poll(name, priority, DRIVING, DOOR_OPEN, CHARGING, CONNECTING, func)`

| slot | 优先级 | DRIVING | DOOR_OPEN | CHARGING | CONNECTING | 说明 |
|------|--------|---------|-----------|----------|------------|------|
| `drive`    | HIGH       | 800ms  | 0 | 0      | 2000ms | 时速/档位/功率 |
| `closures` | HIGH       | 500ms  | 500ms | 60000ms | 0   | 车门/锁状态 |
| `charge`   | MEDIUM     | 120000ms | 0 | 500ms | 2000ms | 电量/充电功率 |
| `vcsec`    | MEDIUM     | 0      | 0 | 0      | 2000ms | 车锁/车门/唤醒（仅会话建立阶段） |
| `climate`  | LOW        | 900000ms | 0 | 0    | 0   | 内外温度（长间隔） |
| `tpms`     | BACKGROUND | 300000ms | 0 | 0    | 0   | 胎压（长间隔） |

要点：
- `vcsec` 只在 `CONNECTING` 启用——车门状态改由 Infotainment 的 `closures` 负责，**避免重复检测**。
- `climate`/`tpms` 是长间隔后台数据，平时几乎不触发。

## 4. 档位联动（`main.cpp` DRIVING 分支）

仅在 `DRIVING` 模式且挂挡后执行：

```cpp
bool parked = (gear == 'P');
scheduler.set_slot_enabled("closures", parked);   // 仅 P 档轮询车门/充电
scheduler.set_slot_enabled("charge",   parked);
```

- **P 档**：`closures` + `charge` 启用（检测开门/充电）。
- **D/R/N/? 档**：`closures` + `charge` 全部禁用，带宽让给 `drive`（时速/档位）。

### TPMS 特殊处理（减少对车机-轮胎传感器通信的干扰）

- 挂 P 档：重置计数，`tpms` 启用。
- 刚离开 P 档：允许 `tpms` 前 2 次轮询。
- 行驶中超过 2 次：`tpms` 禁用，直到下次回到 P 档。

## 5. 冷却（`poll_scheduler.cpp::current_cooldown_ms`）

统计当前**启用且 interval ∈ (0, 10000ms]** 的 slot 数量（长间隔 slot 不计入，避免 climate/tpms/charge@P 拖慢快速 slot）：

| 近程活跃 slot 数 | 冷却 |
|------------------|------|
| ≤1 | 400ms |
| 2  | 600ms |
| ≥3 | 1000ms |

## 6. 派发规则（`poll_scheduler.cpp::process`）

1. **静默窗**：`quiet_` 为 true 时跳过一切（当前恒 false，见第 7 节）。
2. **冷却**：距上次派发不足 `current_cooldown_ms()` 则跳过。
3. **背压**：命令队列深度 ≥ 3（`BACKPRESSURE_THRESHOLD`）则拒绝本次派发。
4. **选最逾期**：在所有已启用且到期的 slot 中，按 `逾期倍数 + 优先级加成` 选最高分者派发。
   - 优先级加成：HIGH=0.2 / MEDIUM=0.1 / LOW=0.05 / BACKGROUND=0（仅平手时起作用）。

## 7. TPMS 静默窗（当前状态）

`quiet_` 字段与 `set_quiet()/is_quiet()` 接口仍在，但**当前恒为 false**——已回退为「连续轮询 + 最小化 BLE 负载」策略（commit `b10b532`）。之前的静默窗方案会导致时速响应变慢，故弃用。

---

## 变更日志

| 日期 | 变更 | 说明 |
|------|------|------|
| 2026-08-25 | 初始文档 | 记录当前实际轮询状态 |
