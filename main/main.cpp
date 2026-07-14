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
// 这保证了主线的蓝牙与校验逻辑完好无损。
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

    ESP_LOGI(TAG, "Entering UI simulation loop (8s Carousel)...");

    while (true) {
        // 获取开机以来的微秒并转换为毫秒
        int64_t uptime_ms = esp_timer_get_time() / 1000;
        
        // 3 页面轮播时序逻辑：每 8000 毫秒换一页
        // page_id: 0 = 驾驶页面, 1 = 开门页面, 2 = 充电页面
        int page_id = (uptime_ms / 8000) % 3;
        int64_t page_offset_ms = uptime_ms % 8000; // 页面内部时序偏移 (0-7999ms)

        // ─── A. 构造全局共有遥测参数 ───
        current_data.valid = true;
        current_data.vehicle_awake = true;
        current_data.ble_connected = false; // DEMO 离线模式
        current_data.odometer_km = 68420;   // 模拟总里程
        current_data.inside_temp = 22.5f;   // 模拟车温
        current_data.outside_temp = 16.0f;  // 模拟外温
        current_data.tpms_fl = 2.5f;        // 前左胎压
        current_data.tpms_fr = 2.6f;        // 前右胎压
        current_data.tpms_rl = 2.4f;        // 后左胎压
        current_data.tpms_rr = 2.5f;        // 后右胎压

        // ─── B. 根据 page_id 构造特定页面的模拟数据 ───
        if (page_id == 0) {
            // ─── 驾驶页面 (0 - 8000ms) ───
            current_data.door_open_fl = false;
            current_data.door_open_fr = false;
            current_data.door_open_rl = false;
            current_data.door_open_rr = false;
            current_data.door_open_trunk_front = false;
            current_data.door_open_trunk_rear = false;
            current_data.charging = false;
            current_data.locked = true;

            // 1. 模拟车速平滑爬升与下降
            float speed_ratio;
            if (page_offset_ms < 5000) {
                // 前5秒平滑加速 (0 到 105 km/h)
                speed_ratio = page_offset_ms / 5000.0f;
                current_data.speed_kmh = speed_ratio * 105.0f;
                current_data.motor_power_kw = speed_ratio * 75.6f;
            } else {
                // 后3秒平滑减速 (动能回收)
                speed_ratio = (8000 - page_offset_ms) / 3000.0f;
                current_data.speed_kmh = speed_ratio * 105.0f;
                current_data.motor_power_kw = -35.2f * speed_ratio;
            }

            // 2. 模拟档位变换特写交互
            // 设定在 0 - 1500ms 为 P 档，之后切换为 D 档。
            if (page_offset_ms < 1500) {
                current_data.gear = 'P';
            } else {
                current_data.gear = 'D';
            }

            current_data.battery_level = 78;
            current_data.battery_range_km = 312.0f;

        } else if (page_id == 1) {
            // ─── 开门页面 (8000 - 16000ms) ───
            current_data.speed_kmh = 0.0f;
            current_data.gear = 'P';
            current_data.charging = false;
            current_data.locked = false;

            // 模拟门在 8 秒内依次开启
            if (page_offset_ms < 2500) {
                // 前2.5秒只开主驾门
                current_data.door_open_fl = true;
                current_data.door_open_fr = false;
                current_data.door_open_trunk_rear = false;
            } else if (page_offset_ms < 5000) {
                // 2.5到5秒开主驾门+副驾门
                current_data.door_open_fl = true;
                current_data.door_open_fr = true;
                current_data.door_open_trunk_rear = false;
            } else {
                // 5秒后加上开启后备箱
                current_data.door_open_fl = true;
                current_data.door_open_fr = true;
                current_data.door_open_trunk_rear = true;
            }
            
            current_data.door_open_rl = false;
            current_data.door_open_rr = false;
            current_data.door_open_trunk_front = false;

        } else {
            // ─── 充电页面 (16000 - 24000ms) ───
            current_data.door_open_fl = false;
            current_data.door_open_fr = false;
            current_data.door_open_rl = false;
            current_data.door_open_rr = false;
            current_data.door_open_trunk_front = false;
            current_data.door_open_trunk_rear = false;

            current_data.speed_kmh = 0.0f;
            current_data.gear = 'P';
            current_data.locked = true;
            
            current_data.charging = true;
            current_data.charging_state_str = "Charging";

            float ratio = page_offset_ms / 8000.0f;
            current_data.battery_level = 75 + (int)(ratio * 5.0f);
            current_data.battery_range_km = 300.0f + current_data.battery_level * 4.0f;

            current_data.charge_power_kw = 11.5f;
            current_data.charge_limit_soc = 80;
            current_data.minutes_to_charge_limit = 45 - (int)(ratio * 5.0f);
        }

        // ─── C. 提交渲染 ───
        display.render_dashboard(current_data);

        // 50ms 频率刷新（20 FPS，肉眼极度顺滑且 CPU 开销微乎其微）
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
