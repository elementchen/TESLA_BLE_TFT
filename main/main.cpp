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

    ESP_LOGI(TAG, "Entering Driving telemetry simulation (No Flicker, 2s per step)...");

    int last_frame_id = -1;

    while (true) {
        int64_t uptime_ms = esp_timer_get_time() / 1000;
        
        // 共有 8 个精细驾驶场景，每隔 2000 毫秒 (2秒) 循环轮转，总周期 16 秒
        int frame_id = (uptime_ms / 2000) % 8;

        // 按需渲染：仅在场景发生改变时重绘一次，2秒内屏幕完全静止，彻底消灭闪烁！
        if (frame_id != last_frame_id) {
            last_frame_id = frame_id;

            // ─── A. 初始化全局共有遥测参数 ───
            current_data.valid = true;
            current_data.vehicle_awake = true;
            current_data.ble_connected = false;
            current_data.odometer_km = 68420;
            current_data.locked = true;
            
            // 锁定在驾驶主页面：确保门全关、不充电
            current_data.door_open_fl = false;
            current_data.door_open_fr = false;
            current_data.door_open_rl = false;
            current_data.door_open_rr = false;
            current_data.door_open_trunk_front = false;
            current_data.door_open_trunk_rear = false;
            current_data.charging = false;

            // ─── B. 根据 frame_id 加载特定的精细驾驶场景 ───
            switch (frame_id) {
                case 0:
                    // 场景 0：车辆静止，P档 (大字红色 P)
                    ESP_LOGI(TAG, "SCENARIO 0: Stationary P-Gear Zoom");
                    current_data.gear = 'P';
                    current_data.speed_kmh = 0.0f;
                    current_data.motor_power_kw = 0.0f;
                    current_data.battery_level = 78;
                    current_data.battery_range_km = 312.0f;
                    current_data.inside_temp = 22.0f;
                    current_data.outside_temp = 16.0f;
                    current_data.tpms_fl = 2.5f;
                    current_data.tpms_fr = 2.5f;
                    current_data.tpms_rl = 2.4f;
                    current_data.tpms_rr = 2.4f;
                    break;

                case 1:
                    // 场景 1：挂入 D 档起步特写 (刚换挡，大字红色 D 特写显示)
                    ESP_LOGI(TAG, "SCENARIO 1: Shift to D-Gear Zoom");
                    current_data.gear = 'D'; // P 变 D 触发 2 秒特写大字 D 档显示
                    current_data.speed_kmh = 0.0f;
                    current_data.motor_power_kw = 0.0f;
                    current_data.battery_level = 78;
                    current_data.battery_range_km = 312.0f;
                    current_data.inside_temp = 22.0f;
                    current_data.outside_temp = 16.0f;
                    current_data.tpms_fl = 2.5f;
                    current_data.tpms_fr = 2.5f;
                    current_data.tpms_rl = 2.4f;
                    current_data.tpms_rr = 2.4f;
                    break;

                case 2:
                    // 场景 2：中速加速行驶 (时速 45，红色功率条向右扩展)
                    ESP_LOGI(TAG, "SCENARIO 2: Running 45 km/h, Accel +28.5kW");
                    current_data.gear = 'D';
                    current_data.speed_kmh = 45.0f;       // 大字车速显示 45
                    current_data.motor_power_kw = 28.5f;  // 加速功率输出
                    current_data.battery_level = 78;
                    current_data.battery_range_km = 311.0f;
                    current_data.inside_temp = 22.1f;
                    current_data.outside_temp = 16.0f;
                    current_data.tpms_fl = 2.5f;
                    current_data.tpms_fr = 2.5f;
                    current_data.tpms_rl = 2.4f;
                    current_data.tpms_rr = 2.4f;
                    break;

                case 3:
                    // 场景 3：高速强力加速 (时速 110，大片红色功率条，高速运转胎压温升)
                    ESP_LOGI(TAG, "SCENARIO 3: Running 110 km/h, Power +88.0kW");
                    current_data.gear = 'D';
                    current_data.speed_kmh = 110.0f;
                    current_data.motor_power_kw = 88.0f; // 大负荷
                    current_data.battery_level = 77;      // 消耗 1%
                    current_data.battery_range_km = 308.0f;
                    current_data.inside_temp = 22.3f;
                    current_data.outside_temp = 16.0f;
                    // 高速下摩擦轮胎升温上涨！
                    current_data.tpms_fl = 2.6f;
                    current_data.tpms_fr = 2.7f;
                    current_data.tpms_rl = 2.5f;
                    current_data.tpms_rr = 2.6f;
                    break;

                case 4:
                    // 场景 4：松开电门滑行 (时速 98，轻微动能回收，绿色功率条向左伸出)
                    ESP_LOGI(TAG, "SCENARIO 4: Coasting 98 km/h, Regen -8.2kW");
                    current_data.gear = 'D';
                    current_data.speed_kmh = 98.0f;
                    current_data.motor_power_kw = -8.2f; // 轻微动能回收 (绿色)
                    current_data.battery_level = 77;
                    current_data.battery_range_km = 309.0f;
                    current_data.inside_temp = 22.4f;
                    current_data.outside_temp = 16.0f;
                    current_data.tpms_fl = 2.6f;
                    current_data.tpms_fr = 2.7f;
                    current_data.tpms_rl = 2.5f;
                    current_data.tpms_rr = 2.6f;
                    break;

                case 5:
                    // 场景 5：重踩刹车减速 (时速 52，大片绿色回收功率条)
                    ESP_LOGI(TAG, "SCENARIO 5: Braking 52 km/h, Heavy Regen -48.5kW");
                    current_data.gear = 'D';
                    current_data.speed_kmh = 52.0f;
                    current_data.motor_power_kw = -48.5f; // 重回收 (高亮绿)
                    current_data.battery_level = 77;
                    current_data.battery_range_km = 310.0f; // 能量回收带来里程稍微增长
                    current_data.inside_temp = 22.4f;
                    current_data.outside_temp = 16.0f;
                    current_data.tpms_fl = 2.6f;
                    current_data.tpms_fr = 2.7f;
                    current_data.tpms_rl = 2.5f;
                    current_data.tpms_rr = 2.6f;
                    break;

                case 6:
                    // 场景 6：挂入 R 档倒车 (时速 5，大字红色 R 特写显示)
                    ESP_LOGI(TAG, "SCENARIO 6: Shift to R-Gear Zoom, Speed 5");
                    current_data.gear = 'R'; // D 变 R 触发特写大字 R 显示
                    current_data.speed_kmh = 5.0f;
                    current_data.motor_power_kw = 12.0f;
                    current_data.battery_level = 77;
                    current_data.battery_range_km = 309.0f;
                    current_data.inside_temp = 22.5f;
                    current_data.outside_temp = 15.5f;
                    current_data.tpms_fl = 2.5f;
                    current_data.tpms_fr = 2.6f;
                    current_data.tpms_rl = 2.4f;
                    current_data.tpms_rr = 2.5f;
                    break;

                case 7:
                    // 场景 7：停稳挂回 P 档 (大字红色 P，车内空调微微升温)
                    ESP_LOGI(TAG, "SCENARIO 7: Parking complete, Back to P-Gear");
                    current_data.gear = 'P';
                    current_data.speed_kmh = 0.0f;
                    current_data.motor_power_kw = 0.0f;
                    current_data.battery_level = 77;
                    current_data.battery_range_km = 309.0f;
                    current_data.inside_temp = 23.0f;
                    current_data.outside_temp = 15.5f;
                    current_data.tpms_fl = 2.5f;
                    current_data.tpms_fr = 2.6f;
                    current_data.tpms_rl = 2.4f;
                    current_data.tpms_rr = 2.5f;
                    break;
            }

            // ─── C. 触发单次极速清屏重绘 ───
            display.render_dashboard(current_data);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
