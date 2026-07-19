#pragma once

#include <cstdint>

#include <string>

struct DashData {
    float speed_kmh = 0.0f;
    char  gear = 'P';       // 'P', 'R', 'N', 'D', 'S'
    uint32_t odometer_km = 0;
    bool valid = false;
    bool vehicle_awake = false;
    bool ble_connected = false;

    // ─── 扩展遥测数据段 ───
    float motor_power_kw = 0.0f;     // 电机功率 (负值为回收，正值为输出)

    // 充电数据
    bool charging = false;           // 连接充电枪且在充电中
    float charge_power_kw = 0.0f;    // 充电实时功率 (kW)
    int32_t charge_limit_soc = 80;   // 充电限制上限 (如 80%)
    int32_t minutes_to_charge_limit = 0; // 充电剩余时间 (分钟)
    std::string charging_state_str = "Disconnected"; // "Disconnected"/"Charging"/"Stopped"/"Complete"/"NoPower"

    // 电池与估计续航
    int32_t battery_level = 0;       // 剩余电量 SOC (%)
    float battery_range_km = 0.0f;   // 估计续航距离 (KM)

    // 客舱与外界环境温度
    float inside_temp = 0.0f;        // 车内温度 (℃)
    float outside_temp = 0.0f;       // 车外温度 (℃)

    // 胎压状态 (bar)
    float tpms_fl = 0.0f;            // 前左胎压
    float tpms_fr = 0.0f;            // 前右胎压
    float tpms_rl = 0.0f;            // 后左胎压
    float tpms_rr = 0.0f;            // 后右胎压

    // 车身开口与中控锁定
    bool door_open_fl = false;       // 主驾驶门 (前左)
    bool door_open_fr = false;       // 副驾驶门 (前右)
    bool door_open_rl = false;       // 后排左门 (后左)
    bool door_open_rr = false;       // 后排右门 (后右)
    bool door_open_trunk_front = false; // 前备箱 (Frunk)
    bool door_open_trunk_rear = false;  // 后备箱 (Trunk)
    bool locked = true;              // 整车上锁状态
};

// DriveState.speed_float 单位未知（可能是 mph），转换为 km/h
inline float mph_to_kmh(float mph) { return mph * 1.609344f; }

// DriveState.odometer_in_hundredths_of_a_mile → 公里
inline uint32_t hundredths_mile_to_km(int32_t val) {
    if (val <= 0) return 0;
    return static_cast<uint32_t>((static_cast<double>(val) / 100.0) * 1.609344);
}
