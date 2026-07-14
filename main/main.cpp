#include <cstdio>
#include <string>
#include <memory>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "vehicle.h"
#include "car_server.pb.h"
#include "vcsec.pb.h"

#include "ble_adapter.h"
#include "storage_adapter.h"
#include "display.h"
#include "dash_data.h"

static constexpr const char *TAG = "TeslaDash";

// ---------- 配置 ----------
#ifndef TESLA_VIN
#define TESLA_VIN "LRWYGCFS2PC792568"
#endif

// ---------- 全局对象 ----------
static Display   display;
static DashData  current_data;

// ST7789 SPI LCD 引脚已直接在 display.cpp 中定义
#define OLED_SDA   0
#define OLED_SCL   0
#define OLED_RESET 0

#if 0
// ──────────────────────────────────────────────────────────────────────
// 【演示分支说明】：原本的蓝牙通信、遥测轮询和配对逻辑已被 #if 0 暂时禁用。
// ──────────────────────────────────────────────────────────────────────
static std::shared_ptr<BleAdapterImpl>     ble_adapter;
static std::shared_ptr<StorageAdapterImpl> storage_adapter;
static std::shared_ptr<TeslaBLE::Vehicle>  vehicle;

static DashData  pending_data;
static bool      pending_data_ready = false;
static TickType_t last_poll_time = 0;
static bool      paired = false;

#define PAIR_RESET_GPIO GPIO_NUM_4
static constexpr int PAIR_RESET_HOLD_MS = 5000;

static char gear_from_shift_state(const CarServer_ShiftState &ss) {
    switch (ss.which_type) {
        case CarServer_ShiftState_P_tag:   return 'P';
        case CarServer_ShiftState_R_tag:   return 'R';
        case CarServer_ShiftState_N_tag:   return 'N';
        case CarServer_ShiftState_D_tag:   return 'D';
        case CarServer_ShiftState_SNA_tag: return '?';
        default:                           return '-';
    }
}

static void on_drive_state(const CarServer_DriveState &ds) {
    if (ds.which_optional_speed_float != 0) {
        pending_data.speed_kmh = ds.optional_speed_float.speed_float;
    } else if (ds.which_optional_speed != 0) {
        pending_data.speed_kmh = static_cast<float>(ds.optional_speed.speed);
    }
    pending_data.gear = gear_from_shift_state(ds.shift_state);
    if (ds.which_optional_odometer_in_hundredths_of_a_mile != 0) {
        pending_data.odometer_km = hundredths_mile_to_km(
            ds.optional_odometer_in_hundredths_of_a_mile.odometer_in_hundredths_of_a_mile);
    }
    pending_data.valid = true;
    pending_data_ready = true;
}

static void on_vehicle_status(const VCSEC_VehicleStatus &status) {
    bool awake = (status.vehicleSleepStatus
                  == VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE);
    current_data.vehicle_awake = awake;
}

static void init_tesla_ble() {
    storage_adapter = std::make_shared<StorageAdapterImpl>();
    ble_adapter     = std::make_shared<BleAdapterImpl>();
    ble_adapter->set_data_callback([](const std::vector<uint8_t> &data) {
        if (vehicle) vehicle->on_rx_data(data);
    });
    ble_adapter->set_status_callback([](bool connected) {
        if (vehicle) {
            vehicle->set_connected(connected);
            if (!connected) vehicle->clear_commands();
        }
        current_data.ble_connected = connected;
        if (!connected) current_data.valid = false;
    });
    vehicle = std::make_shared<TeslaBLE::Vehicle>(ble_adapter, storage_adapter);
    vehicle->set_vin(TESLA_VIN);
    vehicle->set_drive_state_callback(on_drive_state);
    vehicle->set_vehicle_status_callback(on_vehicle_status);
}
#endif

// ---------- 初始化显示屏幕 ----------
static void init_display() {
    if (!display.init(OLED_SDA, OLED_SCL, OLED_RESET)) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    display.show_splash();
    ESP_LOGI(TAG, "Display ready");
}

// ─── 演示分支 app_main 入口 ──────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== Tesla BLE Dashboard (UI Design Branch) ===");
    
    init_display();
    
    // 开机 Splash 停留 2 秒
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Entering 20 FPS high-fidelity differential telemetry stream...");

    int32_t frame_counter = 0;

    // 初始化遥测数据初值
    current_data.valid = true;
    current_data.vehicle_awake = true;
    current_data.ble_connected = false;
    current_data.locked = true;
    current_data.odometer_km = 68420;
    
    current_data.door_open_fl = false;
    current_data.door_open_fr = false;
    current_data.door_open_rl = false;
    current_data.door_open_rr = false;
    current_data.door_open_trunk_front = false;
    current_data.door_open_trunk_rear = false;
    current_data.charging = false;

    current_data.inside_temp = 22.0f;
    current_data.outside_temp = 16.0f;
    current_data.tpms_fl = 2.5f;
    current_data.tpms_fr = 2.5f;
    current_data.tpms_rl = 2.4f;
    current_data.tpms_rr = 2.4f;
    current_data.battery_level = 78;
    current_data.battery_range_km = 312.0f;

    while (true) {
        // 28 秒一个完整物理周期 (每秒 20 帧，总 560 帧)
        int loop_frame = frame_counter % 560;

        // ─── 根据时间步平滑推算“高保真数据流” ───
        
        // 阶段 0 (0 - 60 帧，0-3秒)：静止 P 档
        if (loop_frame >= 0 && loop_frame < 60) {
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            
            current_data.tpms_fl = 2.5f;
            current_data.tpms_fr = 2.5f;
            current_data.tpms_rl = 2.4f;
            current_data.tpms_rr = 2.4f;
        }
        // 阶段 1 (60 - 100 帧，3-5秒)：挂入 D 档起步确认
        else if (loop_frame >= 60 && loop_frame < 100) {
            current_data.gear = 'D'; // P 变 D 触发 2 秒特写大字 D 显示
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
        }
        // 阶段 2 (100 - 180 帧，5-9秒)：轻油门起步加速到 60 km/h
        else if (loop_frame >= 100 && loop_frame < 180) {
            float ratio = (loop_frame - 100) / 80.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = ratio * 60.0f;
            current_data.motor_power_kw = 25.0f;
            
            if (loop_frame % 20 == 0) {
                current_data.odometer_km++;
                current_data.battery_range_km -= 0.1f;
            }
        }
        // 阶段 3 (180 - 260 帧，9-13秒)：强力大负荷加速到 120 km/h，轮胎生热升压
        else if (loop_frame >= 180 && loop_frame < 260) {
            float ratio = (loop_frame - 180) / 80.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = 60.0f + ratio * 60.0f;
            current_data.motor_power_kw = 25.0f + ratio * 70.0f;
            
            current_data.tpms_fl = 2.5f + ratio * 0.1f;
            current_data.tpms_fr = 2.5f + ratio * 0.2f;
            current_data.tpms_rl = 2.4f + ratio * 0.1f;
            current_data.tpms_rr = 2.4f + ratio * 0.2f;
        }
        // 阶段 4 (260 - 340 帧，13-17秒)：松电门滑行，动能回收绿色介入
        else if (loop_frame >= 260 && loop_frame < 340) {
            float ratio = (loop_frame - 260) / 80.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = 120.0f - ratio * 20.0f;
            current_data.motor_power_kw = -12.5f;
        }
        // 阶段 5 (340 - 420 帧，17-21秒)：重踩刹车急减速，强力动能回收大片绿色
        else if (loop_frame >= 340 && loop_frame < 420) {
            float ratio = (loop_frame - 340) / 80.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = 100.0f - ratio * 90.0f;
            current_data.motor_power_kw = -12.5f - ratio * 42.5f;
            
            if (loop_frame % 20 == 0) {
                current_data.battery_range_km += 0.1f;
            }
        }
        // 阶段 6 (420 - 480 帧，21-24秒)：挂入 R 档倒车特写与倒车速度
        else if (loop_frame >= 420 && loop_frame < 480) {
            current_data.gear = 'R'; // D 变 R 触发特写大字 R 显示
            if (loop_frame < 460) {
                current_data.speed_kmh = 0.0f;
            } else {
                current_data.speed_kmh = 5.0f;
            }
            current_data.motor_power_kw = 10.0f;
            
            float ratio = (loop_frame - 420) / 60.0f;
            current_data.tpms_fl = 2.6f - ratio * 0.1f;
            current_data.tpms_fr = 2.7f - ratio * 0.2f;
            current_data.tpms_rl = 2.5f - ratio * 0.1f;
            current_data.tpms_rr = 2.6f - ratio * 0.2f;
        }
        // 阶段 7 (480 - 560 帧，24-28秒)：重新切回 P 档静止，数据平稳冷却
        else {
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            
            current_data.tpms_fl = 2.5f;
            current_data.tpms_fr = 2.5f;
            current_data.tpms_rl = 2.4f;
            current_data.tpms_rr = 2.4f;
            
            if (loop_frame == 500) {
                current_data.inside_temp = 23.0f;
            }
        }

        // 调用“局部差分重绘引擎”进行流畅的 0 全屏重绘渲染
        display.render_dashboard(current_data);

        frame_counter++;
        // 以 20 FPS（每 50ms 一帧）高频模拟真实运行环境，完全不阻塞
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
