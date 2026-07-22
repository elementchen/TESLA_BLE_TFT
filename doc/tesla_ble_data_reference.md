# 特斯拉 BLE 蓝牙遥测数据参考手册

> 依据 [vehicle.proto](https://github.com/yoziru/tesla-ble/blob/main/proto/vehicle.proto) (yoziru/tesla-ble)，同步更新于 2026-07-22。

数据分 **6 个独立 BLE 请求类型**，每个请求返回一个 Protobuf 响应包。包内的字段是一次拿完的，不额外消耗 BLE 带宽。

---

## 请求类型总览

| 请求 | 域 | 频率(驾驶) | 包含的关键字段 |
|------|-----|-----------|--------------|
| **DriveState** | Infotainment | 500ms | 时速, 档位, 功率, 里程, 导航 |
| **ClosuresState** | Infotainment | 500ms | 6门+2盖 开闭, 锁, 哨兵, 车窗 |
| **ChargeState** | Infotainment | 60s | 电量, 续航, 充电功率, 充电状态 |
| **ClimateState** | Infotainment | 60s | 内外温度, 空调, 座椅加热 |
| **TirePressureState** | Infotainment | 60s | 四轮胎压, 胎压警报 |
| **VCSEC Status** | VCSEC | 3s | 车辆休眠/唤醒, 锁状态 |

> **关键认知**: DriveState 和 ClosuresState 是**两个独立请求**。同时发送会各占一个命令槽位 (~800ms 车机处理时间)。交替发送是保持低延迟的核心策略。

---

## 🚗 一、DriveState（行驶状态）

**一个包包含以下全部字段。** 功率、时速、档位、里程在同一次请求中返回。

| Protobuf 字段 | 类型 | 单位 | 说明 |
|:---|:---:|:---:|:---|
| `speed_float` | float | mph | 浮点车速（推荐用于速度表，需 ×1.609 转 km/h） |
| `speed` | uint32 | mph | 整数车速 |
| `shift_state` | enum | P/R/N/D/S/SNA | 实时档位 |
| `power` | int32 | kW | 电机功率。**正值=消耗，负值=动能回收** |
| `odometer_in_hundredths_of_a_mile` | int32 | 0.01mi | 总里程（÷100 × 1.609 = km） |
| `active_route_destination` | string | — | 导航目的地名称 |
| `active_route_miles_to_arrival` | float | mi | 导航剩余距离 |
| `active_route_minutes_to_arrival` | float | min | 导航剩余时间 |
| `active_route_energy_at_arrival` | float | % | 到达时预估 SOC |
| `active_route_traffic_minutes_delay` | float | min | 拥堵延迟 |
| `timestamp` | Timestamp | — | 数据时间戳 |

---

## 🔒 二、ClosuresState（车门/锁/窗）

**与 DriveState 分开，独立请求。** 开门模式下单独高频轮询（500ms），驾驶模式下与 DriveState 交替。

| Protobuf 字段 | 类型 | 说明 |
|:---|:---:|:---|
| `door_open_driver_front` | bool | 主驾门 |
| `door_open_driver_rear` | bool | 主驾后排门 |
| `door_open_passenger_front` | bool | 副驾门 |
| `door_open_passenger_rear` | bool | 副驾后排门 |
| `door_open_trunk_front` | bool | 前备箱 (Frunk) |
| `door_open_trunk_rear` | bool | 后备箱 (Trunk) |
| `window_open_driver_front` | bool | 主驾车窗 |
| `window_open_passenger_front` | bool | 副驾车窗 |
| `window_open_driver_rear` | bool | 主驾后排窗 |
| `window_open_passenger_rear` | bool | 副驾后排窗 |
| `locked` | bool | 整车锁定 |
| `is_user_present` | bool | 主驾是否有人 |
| `sentry_mode_state` | enum | 哨兵模式 (Off/Idle/Armed/Aware/Panic) |
| `center_display_state` | enum | 中控屏状态 (Off/On/Driving/Charging/Lock/Sentry) |
| `sun_roof_state` | enum | 天窗状态 |
| `sun_roof_percent_open` | int32 | 天窗开度 % |
| `valet_mode` | bool | 代客模式 |
| `remote_start` | bool | 远程启动激活中 |
| `timestamp` | Timestamp | 数据时间戳 |

---

## 🔋 三、ChargeState（电池与充电）

| Protobuf 字段 | 类型 | 单位 | 说明 |
|:---|:---:|:---:|:---|
| `battery_level` | int32 | % | 剩余电量 SOC（电池图标用） |
| `usable_battery_level` | int32 | % | 可用电量（扣除寒冷受限） |
| `battery_range` | float | mi | 额定续航 |
| `est_battery_range` | float | mi | 估计续航（基于能耗） |
| `ideal_battery_range` | float | mi | 理想续航 |
| `charging_state` | enum | — | Disconnected/Charging/Stopped/Complete/NoPower |
| `charger_power` | int32 | kW | 实时充电功率 |
| `charger_voltage` | int32 | V | 充电电压 |
| `charger_actual_current` | int32 | A | 充电电流 |
| `charge_limit_soc` | int32 | % | 充电上限（如 80%） |
| `minutes_to_full_charge` | int32 | min | 充满剩余时间 |
| `minutes_to_charge_limit` | int32 | min | 到上限剩余时间 |
| `charge_port_door_open` | bool | — | 充电口开闭 |
| `charge_port_color` | enum | — | 充电口指示灯颜色 |
| `charge_port_latch` | enum | — | 充电枪锁止状态 |
| `charge_rate_mph` | int32 | mi/h | 充电速率 |
| `scheduled_charging_start_time` | uint64 | — | 预约充电时间 |
| `preconditioning_enabled` | bool | — | 预调节开启 |
| `managed_charging_active` | bool | — | 智能充电激活 |
| `powershare_status` | enum | — | 外放电状态 |
| `timestamp` | Timestamp | — | 数据时间戳 |

---

## 🌡 四、ClimateState（空调与温度）

| Protobuf 字段 | 类型 | 单位 | 说明 |
|:---|:---:|:---:|:---|
| `inside_temp_celsius` | float | ℃ | **车内温度** |
| `outside_temp_celsius` | float | ℃ | **车外温度** |
| `driver_temp_setting` | float | ℃ | 驾驶位设定温度 |
| `passenger_temp_setting` | float | ℃ | 副驾设定温度 |
| `is_climate_on` | bool | — | 空调开关 |
| `fan_status` | int32 | 0-7 | 风扇挡位 |
| `is_front_defroster_on` | bool | — | 前除霜 |
| `is_rear_defroster_on` | bool | — | 后除霜 |
| `steering_wheel_heater` | bool | — | 方向盘加热 |
| `seat_heater_left` | int32 | 0-3 | 主驾座椅加热 |
| `seat_heater_right` | int32 | 0-3 | 副驾座椅加热 |
| `seat_heater_rear_left` | int32 | 0-3 | 后排左加热 |
| `seat_heater_rear_right` | int32 | 0-3 | 后排右加热 |
| `is_preconditioning` | bool | — | 预调节中 |
| `climate_keeper_mode` | enum | Off/On/Dog/Camp | 驻车空调 |
| `bioweapon_mode_on` | bool | — | 生化防御 (HEPA 车型) |
| `cabin_overheat_protection` | enum | — | 座舱过热保护 |
| `remote_heater_control_enabled` | bool | — | 远程加热 |
| `timestamp` | Timestamp | — | 数据时间戳 |

---

## 🛞 五、TirePressureState（胎压）

| Protobuf 字段 | 类型 | 单位 | 说明 |
|:---|:---:|:---:|:---|
| `tpms_pressure_fl` | float | bar | 前左 |
| `tpms_pressure_fr` | float | bar | 前右 |
| `tpms_pressure_rl` | float | bar | 后左 |
| `tpms_pressure_rr` | float | bar | 后右 |
| `tpms_soft_warning_fl/fr/rl/rr` | bool | — | 胎压偏低警告 |
| `tpms_hard_warning_fl/fr/rl/rr` | bool | — | 严重胎压警报 |
| `tpms_rcp_front_value` | float | bar | 推荐前胎压 |
| `tpms_rcp_rear_value` | float | bar | 推荐后胎压 |
| `timestamp` | Timestamp | — | 数据时间戳 |

---

## 🔐 六、VCSEC VehicleStatus（车辆安全状态）

| 字段 | 类型 | 说明 |
|:---|:---:|:---|
| `vehicleSleepStatus` | enum | AWAKE / ASLEEP |
| `closureStatuses` | array | 各门锁状态汇总 |

---

## 📦 数据包关联关系

```
同一个 BLE 请求 → 同一个响应包（零额外开销）:

DriveState 包:     speed + gear + power + odometer + nav
ClosuresState 包:   doors ×6 + frunk/trunk + locked + windows + sentry
ChargeState 包:     soc + range + charge_power + charge_limit + minutes_remaining
ClimateState 包:    inside_temp + outside_temp + seat_heaters + defrost + fan
TirePressure 包:    tpms×4 + warnings
```

不同包之间**互不影响**——获取 power 不增加任何数据开销，它和 speed 在同一个包里。

---

## ⚡ 当前 ESP32 轮询策略

| 模式 | 活跃请求 | 频率 | 策略 |
|------|---------|------|------|
| 驾驶 | DriveState + ClosuresState | 各 ~1.2s | 交替发送，队列深度 0-1 |
| 开门 | ClosuresState | 500ms 独占 | 最快检测关门 |
| 充电 | ChargeState | 500ms 独占 | 功率实时 |
| 连接中 | DriveState + ChargeState + VCSEC | 2s | 轻量维持会话 |

**低延迟关键**: 同时只活跃 ≤2 种请求类型，保证命令队列不堆积。
