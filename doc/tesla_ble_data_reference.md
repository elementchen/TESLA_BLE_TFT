# 特斯拉 BLE 蓝牙遥测数据参考手册 (Tesla BLE Telemetry Data Reference)

本手册基于 `yoziru/tesla-ble` 协议栈中对应的特斯拉官方 Protobuf 协议定义（[vehicle.proto](file:///e:/AI_coding_test/_Gemini/TESLA_BLE_TFT/components/tesla-ble/proto/vehicle.proto)），为您罗列了**所有可通过低功耗蓝牙 (BLE) 读取到的特斯拉车辆传感器和遥测数据**。

数据按功能划分为六个大模块：**行驶状态 (DriveState)**、**电池与充电 (ChargeState)**、**空调与温度 (ClimateState)**、**车门/锁/窗 (ClosuresState)**、**胎压 (TirePressureState)** 以及 **媒体播放 (MediaState)**。

---

## 🚗 一、行驶状态 (DriveState)
主要描述车辆行驶中的实时物理量和导航路线。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `shift_state` | `ShiftState` | `P`/`R`/`N`/`D`/`S`/`SNA` | 实时档位状态 |
| `speed_float` | `float` | km/h 或 mph | 极其精确的实时车速（推荐用于速度表） |
| `speed` | `uint32` | 整数 | 整数车速（过滤掉了浮点波动） |
| `power` | `int32` | kW (千瓦) | 电机功率。**正值**代表正在输出动力，**负值**代表正在进行**动能回收 (Regen)** |
| `odometer_in_hundredths_of_a_mile` | `int32` | 0.01 英里 | 总行驶里程（需乘 `1.609344 / 100.0` 转换为 km） |
| `active_route_destination` | `string` | 文本 | 导航目的地的名称 |
| `active_route_miles_to_arrival` | `float` | 英里 | 距离导航终点剩余路程（英里） |
| `active_route_minutes_to_arrival` | `float` | 分钟 | 预计到达目的地剩余时间 |
| `active_route_energy_at_arrival` | `float` | % | 预计到达目的地时的电池剩余百分比 (SOC) |
| `active_route_traffic_minutes_delay`| `float` | 分钟 | 导航路线中拥堵造成的延迟时间 |

---

## 🔋 二、电池与充电状态 (ChargeState)
主要描述电池电量、续航和充电物理参数。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `battery_level` | `int32` | % (百分比) | 电池剩余电量 (SOC)，最适合用于电池图标 |
| `usable_battery_level` | `int32` | % (百分比) | 真正可使用的剩余电量（扣除了寒冷天气下的受限电量） |
| `battery_range` | `float` | 英里 | 额定剩余续航距离 (Rated Range) |
| `est_battery_range` | `float` | 英里 | 基于最近行驶能耗评估的估计续航距离 |
| `ideal_battery_range` | `float` | 英里 | 理想工况下的续航距离 |
| `charging_state` | `enum` | Disconnected/Charging/Stopped/Complete/NoPower | 当前充电连接状态 |
| `charger_voltage` | `int32` | V (伏特) | 充电输入电压（如 220V 或 380V） |
| `charger_actual_current` | `int32` | A (安培) | 充电实际电流 |
| `charger_power` | `int32` | kW (千瓦) | 实时充电功率 |
| `minutes_to_full_charge` | `int32` | 分钟 | 距离充到 100% 充满所需的剩余时间 |
| `minutes_to_charge_limit` | `int32` | 分钟 | 距离充到您设置的充电上限 (如 80%) 的剩余时间 |
| `charge_limit_soc` | `int32` | % (百分比) | 车主设定的充电限值/上限 (例如 80% 或 90%) |
| `charge_rate_mph` | `int32` | 英里/小时 | 充电补能速率（每小时增加的续航里程） |
| `charge_port_door_open` | `bool` | `true` / `false` | 充电口舱门是否打开 |
| `charge_port_color` | `enum` | Red/Green/Blue/White/Flashing | 充电口物理指示灯的实时颜色 |

---

## 🌡 三、空调与温度状态 (ClimateState)
主要描述空调开关、车内温度、座椅方向盘加热等。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `is_climate_on` | `bool` | `true` / `false` | 空调/气候控制系统是否开启 |
| `inside_temp_celsius` | `float` | ℃ | **车内温度**（非常实用） |
| `outside_temp_celsius` | `float` | ℃ | **车外温度**（车机屏幕显示的室外温度） |
| `driver_temp_setting` | `float` | ℃ | 驾驶位设定温度 |
| `passenger_temp_setting` | `float` | ℃ | 副驾驶位设定温度 |
| `fan_status` | `int32` | 0 - 7 (挡位) | 风扇风速挡位 |
| `steering_wheel_heater` | `bool` | `true` / `false` | 方向盘加热是否开启 |
| `seat_heater_left` | `int32` | 0 (关) - 3 (高) | 主驾驶座椅加热等级 |
| `seat_heater_right` | `int32` | 0 (关) - 3 (高) | 副驾驶座椅加热等级 |
| `seat_heater_rear_left`/`right` | `int32` | 0 (关) - 3 (高) | 后排座椅加热等级 |
| `is_preconditioning` | `bool` | `true` / `false` | 车辆是否正在进行电池/客舱预热 |
| `climate_keeper_mode` | `enum` | Off/On/Dog/Camp (宠物/露营) | 空调驻车保持模式 |
| `bioweapon_mode_on` | `bool` | `true` / `false` | 生化武器防御模式是否开启（HEPA滤网车型） |

---

## 🔒 四、车门/锁/窗状态 (ClosuresState)
主要描述车身安全状况和车主在车在场状态。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `locked` | `bool` | `true` (已锁) / `false` | **整车车门是否上锁** |
| `is_user_present` | `bool` | `true` / `false` | **主驾驶位是否有人**（用于判断车主是否上车） |
| `door_open_driver_front` | `bool` | `true` (开启) / `false` | 主驾驶门是否打开 |
| `door_open_passenger_front` | `bool` | `true` (开启) / `false` | 副驾驶门是否打开 |
| `door_open_trunk_front` (Frunk) | `bool` | `true` (开启) / `false` | **前备箱**是否打开 |
| `door_open_trunk_rear` (Trunk) | `bool` | `true` (开启) / `false` | **后备箱**是否打开 |
| `window_open_driver_front` | `bool` | `true` / `false` | 主驾驶侧前车窗是否降下 |
| `sentry_mode_state` | `enum` | Off/Idle/Armed/Panic | 哨兵模式的当前警戒状态 |
| `center_display_state` | `enum` | Off/On/Driving/Lock/Sentry | **中控大屏的实时工作状态** |

---

## 🛞 五、胎压传感器状态 (TirePressureState)
用于绘制车辆四轮实时胎压。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `tpms_pressure_fl` | `float` | **bar** (巴) | **前左轮胎压** (乘以 14.5038 可换算为 PSI) |
| `tpms_pressure_fr` | `float` | **bar** (巴) | **前右轮胎压** |
| `tpms_pressure_rl` | `float` | **bar** (巴) | **后左轮胎压** |
| `tpms_pressure_rr` | `float` | **bar** (巴) | **后右轮胎压** |
| `tpms_soft_warning_fl` / `fr`等 | `bool` | `true` / `false` | 对应轮胎是否触发“胎压偏低警告” |
| `tpms_hard_warning_fl` / `fr`等 | `bool` | `true` / `false` | 对应轮胎是否触发“严重胎压警报” |

---

## 🎵 六、媒体与音影播放 (MediaState)
用于做歌词、歌名或者媒体信息显示。

| Protobuf 字段名 | C++ 类型 | 单位/枚举值 | 数据说明 |
| :--- | :---: | :---: | :--- |
| `now_playing_title` | `string` | 文本 | 正在播放的**歌曲名/节目名** |
| `now_playing_artist` | `string` | 文本 | 正在播放的**歌手名/广播台** |
| `now_playing_album` | `string` | 文本 | 正在播放的专辑名称 |
| `audio_volume` | `float` | 0.0 - Max | 媒体系统的当前音量百分比 |
| `now_playing_duration` | `int32` | 毫秒 (ms) | 正在播放歌曲的总时长 |
| `now_playing_elapsed` | `int32` | 毫秒 (ms) | 正在播放歌曲的当前进度时间 |

---

## 💻 C++ 代码中提取与订阅范例

当特斯拉通过蓝牙向 ESP32 返回数据包时，底层的 `tesla-ble` 协议栈会调用注册的回调。在我们的项目中，您可以按照以下方式直接提取扩展数据：

```cpp
#include "vehicle.pb.h" // 乐鑫下由 vehicle.proto 自动生成的头文件

// 可以在 main.cpp 中扩展的数据接收回调
static void on_drive_state(const CarServer_DriveState &state) {
    // 1. 获取浮点时速并转换
    if (state.has_speed_float) {
        float speed_mph = state.speed_float;
        float speed_kmh = speed_mph * 1.609344f;
        ESP_LOGI("TELEMETRY", "Current Speed: %.2f km/h", speed_kmh);
    }
    
    // 2. 获取实时电机功率 (kW)
    if (state.has_power) {
        int32_t raw_power = state.power; // 负值代表回收, 正值代表消耗
        ESP_LOGI("TELEMETRY", "Motor Power: %d kW", raw_power);
    }
}

static void on_charge_state(const CarServer_ChargeState &state) {
    // 3. 获取电池 SOC 百分比
    if (state.has_battery_level) {
        int32_t soc = state.battery_level;
        ESP_LOGI("TELEMETRY", "Battery SOC: %d%%", soc);
    }
    
    // 4. 获取充电功率
    if (state.has_charger_power) {
        int32_t charge_kw = state.charger_power;
        ESP_LOGI("TELEMETRY", "Charging Power: %d kW", charge_kw);
    }
}
```

这些丰富的数据段可以给您在设计 320x240 大屏时带来无限可能（例如：充电时自动切换为大字体绿色充电界面、主控温度过高时提示、根据电机功率画出随油门变色的实时马力条、展示四轮实时胎压、甚至展示大屏当前播放的歌手等）！
