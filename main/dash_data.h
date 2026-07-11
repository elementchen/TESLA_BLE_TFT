#pragma once

#include <cstdint>

struct DashData {
    float speed_kmh = 0.0f;
    char  gear = 'P';       // 'P', 'R', 'N', 'D'
    uint32_t odometer_km = 0;
    bool valid = false;
    bool vehicle_awake = false;
    bool ble_connected = false;
};

// DriveState.speed_float 单位未知（可能是 mph），转换为 km/h
inline float mph_to_kmh(float mph) { return mph * 1.609344f; }

// DriveState.odometer_in_hundredths_of_a_mile → 公里
inline uint32_t hundredths_mile_to_km(int32_t val) {
    if (val <= 0) return 0;
    return static_cast<uint32_t>((static_cast<double>(val) / 100.0) * 1.609344);
}
