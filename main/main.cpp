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

    ESP_LOGI(TAG, "Entering 20 FPS scenario-based telemetry script...");

    int32_t frame_counter = 0;

    // 默认基础状态
    current_data.valid = true;
    current_data.vehicle_awake = true;
    current_data.ble_connected = false; // DEMO 演示模式
    current_data.locked = true;
    current_data.odometer_km = 68420;
    
    current_data.door_open_fl = false;
    current_data.door_open_fr = false;
    current_data.door_open_rl = false;
    current_data.door_open_rr = false;
    current_data.door_open_trunk_front = false;
    current_data.door_open_trunk_rear = false;
    current_data.charging = false;

    current_data.inside_temp = 35.0f; // 暴晒车内高温
    current_data.outside_temp = 16.0f;
    current_data.tpms_fl = 2.3f;
    current_data.tpms_fr = 2.3f;
    current_data.tpms_rl = 2.3f;
    current_data.tpms_rr = 2.3f;
    current_data.battery_level = 62;
    current_data.battery_range_km = 248.0f;

    while (true) {
        // 35 秒一个完整剧本周期 (每秒 20 帧，总 700 帧)
        int loop_frame = frame_counter % 700;

        // ─── 阶段 0：解锁上车（0.0 - 5.0秒，0 - 100帧） ───
        if (loop_frame >= 0 && loop_frame < 100) {
            current_data.locked = false;
            current_data.door_open_fl = true;  // 主驾门开
            current_data.door_open_rl = true;  // 左后门开
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            current_data.charging = false;
        }
        // ─── 阶段 1：准备起步（5.0 - 8.0秒，100 - 160帧） ───
        else if (loop_frame >= 100 && loop_frame < 160) {
            current_data.door_open_fl = false; // 关门
            current_data.door_open_rl = false;
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 1.5f; // 空调出风降温功率
        }
        // ─── 阶段 2：D档轻油门起步（8.0 - 12.0秒，160 - 240帧） ───
        else if (loop_frame >= 160 && loop_frame < 240) {
            current_data.gear = 'D'; // 挂 D 档 (前 40 帧为特写，后 40 帧加速)
            if (loop_frame < 200) {
                current_data.speed_kmh = 0.0f;
                current_data.motor_power_kw = 0.0f;
            } else {
                float ratio = (loop_frame - 200) / 40.0f;
                current_data.speed_kmh = ratio * 30.0f;
                current_data.motor_power_kw = 12.0f;
            }
            if (loop_frame % 20 == 0) {
                current_data.odometer_km++;
                current_data.battery_range_km -= 0.1f;
            }
        }
        // ─── 阶段 3：强力加速超车（12.0 - 18.0秒，240 - 360帧） ───
        else if (loop_frame >= 240 && loop_frame < 360) {
            float ratio = (loop_frame - 240) / 120.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = 30.0f + ratio * 90.0f;
            current_data.motor_power_kw = 25.0f + ratio * 60.0f;
            
            // 轮胎摩擦生热升压
            current_data.tpms_fl = 2.3f + ratio * 0.3f;
            current_data.tpms_fr = 2.3f + ratio * 0.3f;
            current_data.tpms_rl = 2.3f + ratio * 0.2f;
            current_data.tpms_rr = 2.3f + ratio * 0.2f;
            
            // 车内快速降温
            current_data.inside_temp = 35.0f - ratio * 13.0f;
            
            if (loop_frame % 15 == 0) {
                current_data.odometer_km++;
                current_data.battery_range_km -= 0.2f;
            }
        }
        // ─── 阶段 4：动能回收与重刹车（18.0 - 23.0秒，360 - 460帧） ───
        else if (loop_frame >= 360 && loop_frame < 460) {
            current_data.gear = 'D';
            if (loop_frame < 410) {
                // 松电门滑行 (120 -> 90 km/h)
                float ratio = (loop_frame - 360) / 50.0f;
                current_data.speed_kmh = 120.0f - ratio * 30.0f;
                current_data.motor_power_kw = -12.5f; // 轻度回收
            } else {
                // 重踩刹车 (90 -> 10 km/h)
                float ratio = (loop_frame - 410) / 50.0f;
                current_data.speed_kmh = 90.0f - ratio * 80.0f;
                current_data.motor_power_kw = -12.5f - ratio * 47.5f; // 强力动能回收
            }
            if (loop_frame % 25 == 0) {
                current_data.battery_range_km += 0.1f; // 续航微涨
            }
        }
        // ─── 阶段 5：挂 R 档倒车（23.0 - 27.0秒，460 - 540帧） ───
        else if (loop_frame >= 460 && loop_frame < 540) {
            current_data.gear = 'R'; // 变 R 挡触发特写
            if (loop_frame < 500) {
                current_data.speed_kmh = 0.0f;
                current_data.motor_power_kw = 0.0f;
            } else {
                current_data.speed_kmh = 5.0f; // 倒车速度 5
                current_data.motor_power_kw = 4.0f;
            }
            // 胎压平稳冷却
            float ratio = (loop_frame - 460) / 80.0f;
            current_data.tpms_fl = 2.6f - ratio * 0.3f;
            current_data.tpms_fr = 2.6f - ratio * 0.3f;
            current_data.tpms_rl = 2.5f - ratio * 0.2f;
            current_data.tpms_rr = 2.5f - ratio * 0.2f;
        }
        // ─── 阶段 6：停稳熄火，插枪充电（27.0 - 35.0秒，540 - 700帧） ───
        else {
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            current_data.locked = true;
            
            // 激活充电状态
            current_data.charging = true;
            current_data.charge_power_kw = 7.2f;
            
            // 模拟进度微增和充电时间倒计时
            float ratio = (loop_frame - 540) / 160.0f;
            current_data.battery_level = 62 + (int)(ratio * 3); // 62% 慢增至 65%
            current_data.battery_range_km = 248.0f + ratio * 12.0f; // 续航上升
            current_data.minutes_to_charge_limit = 225 - (int)(ratio * 15);
        }

        // 调用“多界面差分渲染派发接口”
        display.render_dashboard(current_data);

        frame_counter++;
        // 稳定 20 FPS（50ms 一帧），体验极其丝滑
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
