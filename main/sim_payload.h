#pragma once
#include <cstdint>

#pragma pack(push, 1)
typedef struct {
    float speed_kmh;
    float motor_power_kw;
    int32_t battery_level;
    float battery_range_km;
    char gear;
    uint8_t doors[6]; // fl, fr, rl, rr, frunk, trunk
    uint8_t locked;
    uint8_t charging;
    float charge_power_kw;
    float inside_temp;
    float outside_temp;
    float tpms[4]; // fl, fr, rl, rr
} SimPayload;
#pragma pack(pop)
