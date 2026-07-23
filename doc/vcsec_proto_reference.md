# Tesla VCSEC BLE 协议参考手册

> 依据 [vcsec.proto](https://github.com/yoziru/tesla-ble/blob/main/proto/vcsec.proto) (yoziru/tesla-ble)。
> VCSEC = Vehicle Security，域编号 0，独立于 Infotainment 域 (1)。
> **响应仅需 ~320ms**（对比 Infotainment 的 ~800ms），是获取车门/锁/用户状态的最快通道。

---

## 一、VCSEC 域的作用

| 功能 | 说明 |
|------|------|
| 车辆安全控制 | 锁/解锁、开后备箱/前备箱、开充电口 |
| 车辆唤醒 | 发送 `RKE_ACTION_WAKE_VEHICLE` 唤醒休眠车辆 |
| 密钥白名单 | 添加/删除/查询已授权的蓝牙钥匙 |
| **车辆状态查询** | 车门、锁、休眠、用户存在 — 320ms 快速通道 |
| 会话维护 | SessionInfo 握手（所有域的命令都需要先经过 VCSEC 认证） |

---

## 二、从 VCSEC 可以读取的数据

### VehicleStatus（车辆状态）

**通过 `INFORMATION_REQUEST_GET_STATUS` 请求获取。** 一个响应包包含以下全部字段。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `closureStatuses` → 见下方 ClosureStatuses | message | **6 门 + 充电口 + 天窗盖** 的开关状态 |
| `vehicleLockState` | enum | 整车锁状态 |
| `vehicleSleepStatus` | enum | 休眠/唤醒 |
| `userPresence` | enum | 驾驶员是否在车内 |
| `detailedClosureStatus` | message | 天窗盖开度百分比 |

### ClosureStatuses（车门/盖状态）

**这是 VCSEC 最有价值的数据**，比 Infotainment ClosuresState 更快（320ms vs 800ms）且状态更精细。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `frontDriverDoor` | ClosureState_E | 主驾门 |
| `frontPassengerDoor` | ClosureState_E | 副驾门 |
| `rearDriverDoor` | ClosureState_E | 主驾后排门 |
| `rearPassengerDoor` | ClosureState_E | 副驾后排门 |
| `rearTrunk` | ClosureState_E | 后备箱 |
| `frontTrunk` | ClosureState_E | 前备箱 (Frunk) |
| `chargePort` | ClosureState_E | 充电口盖 |
| `tonneau` | ClosureState_E | 天窗盖（Cybertruck 等） |

### ClosureState_E 枚举值

| 值 | 含义 | ESP32 处理建议 |
|:--:|:---|:---|
| 0 | **CLOSED** — 关闭 | `door_open = false` |
| 1 | **OPEN** — 打开 | `door_open = true` |
| 2 | **AJAR** — 虚掩（没关严） | `door_open = true`（安全起见当作开） |
| 3 | UNKNOWN — 未知 | `door_open = true`（保守处理） |
| 4 | FAILED_UNLATCH — 解锁失败 | `door_open = true` |
| 5 | OPENING — 正在打开 | `door_open = true` |
| 6 | CLOSING — 正在关闭 | `door_open = false`（暂且，等下一帧确认） |

> **与 Infotainment ClosuresState 的区别**：Infotainment 只返回 bool（开/关），VCSEC 能区分 OPEN/AJAR/OPENING/CLOSING。

### VehicleLockState_E

| 值 | 含义 |
|:--:|:---|
| 0 | **UNLOCKED** — 未锁 |
| 1 | **LOCKED** — 已锁 |
| 2 | INTERNAL_LOCKED — 内部锁定（人在车内锁车） |
| 3 | SELECTIVE_UNLOCKED — 仅驾驶位解锁 |

> 当前 ESP32 代码：`locked = (vehicleLockState != UNLOCKED)`，把 1/2/3 都视为"锁"。

### VehicleSleepStatus_E

| 值 | 含义 | ESP32 用途 |
|:--:|:---|:---|
| 0 | UNKNOWN | — |
| 1 | **AWAKE** — 车机清醒 | 可正常轮询 |
| 2 | **ASLEEP** — 休眠中 | 暂停 Infotainment 轮询，仅发 VCSEC 唤醒 |

### UserPresence_E

| 值 | 含义 | ESP32 用途 |
|:--:|:---|:---|
| 0 | UNKNOWN | — |
| 1 | **NOT_PRESENT** — 无人 | — |
| 2 | **PRESENT** — 有人在驾驶座 | 可作为"已上车"判断 |

---

## 三、VCSEC 可以发送的控制命令

### RKE Action（远程钥匙操作）

| 命令 | 枚举值 | 说明 |
|:---|:---|:---|
| `RKE_ACTION_WAKE_VEHICLE` | — | 唤醒休眠车辆 |
| `RKE_ACTION_LOCK` | — | 锁车 |
| `RKE_ACTION_UNLOCK` | — | 解锁 |
| `RKE_ACTION_OPEN_TRUNK` | — | 开后备箱 |
| `RKE_ACTION_OPEN_FRUNK` | — | 开前备箱 |
| `RKE_ACTION_CLOSE_TRUNK` | — | 关后备箱 |
| `RKE_ACTION_OPEN_CHARGE_PORT` | — | 开充电口 |
| `RKE_ACTION_CLOSE_CHARGE_PORT` | — | 关充电口 |

### Information Request（状态查询）

| 请求类型 | 说明 |
|:---|:---|
| `INFORMATION_REQUEST_TYPE_GET_STATUS` | 获取 VehicleStatus（门/锁/休眠） |
| `INFORMATION_REQUEST_TYPE_GET_WHITELIST_INFO` | 查询已配对的钥匙列表 |
| `INFORMATION_REQUEST_TYPE_GET_WHITELIST_ENTRY_INFO` | 查询特定钥匙的详细信息 |

### Whitelist（密钥白名单管理）

| 操作 | 说明 |
|:---|:---|
| `PERMISSION_CHANGE_ADD_KEY_TO_WHITELIST` | **配对：添加新钥匙到白名单** |
| `PERMISSION_CHANGE_REMOVE_KEY_FROM_WHITELIST` | 从白名单删除钥匙 |
| `PERMISSION_CHANGE_ADD_KEY_TO_WHITELIST_AND_ADD_PERMISSIONS` | 添加钥匙并同时授权 |

---

## 四、与 Infotainment 域的数据对照

| 数据 | VCSEC 来源 | Infotainment 来源 | 推荐用哪个 |
|------|-----------|-------------------|-----------|
| 车门开闭 | ClosureStatuses (~320ms) | ClosuresState (~800ms) | **VCSEC**（快+细） |
| 整车锁 | vehicleLockState (~320ms) | locked bool (~800ms) | **VCSEC**（快+4态） |
| 休眠状态 | vehicleSleepStatus (~320ms) | — | **VCSEC**（唯一来源） |
| 用户是否在车内 | userPresence (~320ms) | is_user_present (~800ms) | **VCSEC**（快+3态） |
| 充电口盖 | ClosureStatuses.chargePort (~320ms) | charge_port_door_open (~800ms) | **VCSEC**（更快） |
| 时速/档位/功率 | — | DriveState | **Infotainment**（唯一来源） |
| 电量/续航 | — | ChargeState | **Infotainment**（唯一来源） |
| 温度 | — | ClimateState | **Infotainment**（唯一来源） |
| 胎压 | — | TirePressureState | **Infotainment**（唯一来源） |

---

## 五、数据包关联关系

```
VCSEC 域（域编号 0，独立处理资源）:
  ┌─ VCSEC Poll (INFORMATION_REQUEST_GET_STATUS)
  │   发送: 空请求
  │   响应: VehicleStatus {
  │            closureStatuses (门×6 + 充电口 + 天窗盖)
  │            vehicleLockState (锁状态 4态)
  │            vehicleSleepStatus (休眠/唤醒)
  │            userPresence (是否在场)
  │            detailedClosureStatus (天窗盖开度%)
  │          }
  │   耗时: ~320ms（比 Infotainment 快一倍）
  │
  └─ VCSEC SessionInfo
      发送: 公钥 + challenge
      响应: SessionInfo (counter, epoch, vehicle pubkey, HMAC)
      耗时: ~320ms

Infotainment 域（域编号 1，与 VCSEC 共享 BLE 连接但独立处理资源）:
  ├─ DriveState    → 时速/档位/功率/里程 (800ms)
  ├─ ClosuresState → 车门/锁/窗/哨兵       (800ms)
  ├─ ChargeState   → 电量/续航/充电功率    (800ms)
  ├─ ClimateState  → 温度/空调/座椅加热    (800ms)
  └─ TirePressure  → 四轮胎压             (800ms)
```

---

## 六、当前 ESP32 使用情况

| VCSEC 数据 | 是否提取 | 用途 |
|-----------|---------|------|
| `vehicleSleepStatus` | ✅ | 判断车机是否休眠 |
| `closureStatuses` | ✅ (刚加) | 日常驾驶中替代 ClosuresState |
| `vehicleLockState` | ✅ (刚加) | 锁状态 |
| `userPresence` | ❌ | 尚未提取（可替代 is_user_present） |
| `detailedClosureStatus` | ❌ | 天窗盖开度（暂无需求） |
