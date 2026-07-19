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
#include "lvgl.h"

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

// 实车测试蓝牙天线数据 (1) 还是本地假数据模拟数据流演示 (0)
#define USE_REAL_CAR_BLE 0

#if USE_REAL_CAR_BLE
// ──────────────────────────────────────────────────────────────────────
// 【实车连接分支】：原本的蓝牙通信、遥测轮询和配对逻辑。
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
    ESP_LOGI(TAG, "Display ready");
}

// ─── LVGL 1ms 高精度硬件心跳时钟回调 ───
static void lvgl_tick_timer_cb(void *arg) {
    lv_tick_inc(1); // 增加 1ms 心跳滴答
}

// ─── 演示分支 app_main 入口 ──────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== Tesla BLE Dashboard (LVGL UI Design Branch) ===");
    
    init_display();

    #if USE_REAL_CAR_BLE
    init_tesla_ble();
    #endif
    
    // ─── 注册并启动 LVGL 1ms 滴答定时器 ───
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &lvgl_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000)); // 1000us = 1ms

    // 开机等待 1 秒，让初始状态显示
    for (int i = 0; i < 100; i++) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Entering LVGL 100Hz event loop with 20 FPS telemetry scenario...");

    int32_t frame_counter = 0;

    // 默认基础状态
    current_data.valid = true;
    current_data.vehicle_awake = true;
    current_data.ble_connected = false; // DEMO 模式
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

    // 首次上电运行蓝牙配对授权演示的标志位 (之后的循环将直接跳过 Landing 引导页以最大化稳定系统内存)
    static bool first_pairing_demo_done = false;

    while (true) {
        int loop_frame;
        #if USE_REAL_CAR_BLE
        loop_frame = frame_counter % 950;
        #else
        if (!first_pairing_demo_done) {
            loop_frame = frame_counter % 950;
            if (loop_frame == 949) {
                first_pairing_demo_done = true;
                frame_counter = 90; // 强制将下一帧设定为 90 帧起步，避免重入 Landing
                loop_frame = 90;
            }
        } else {
            // 后续所有轮次直接在 90 ~ 949 帧 (行驶、动能回收、倒车、开门、充电) 之间高健壮性循环
            loop_frame = 90 + ((frame_counter - 90) % 860); 
        }
        #endif

        #if USE_REAL_CAR_BLE
        // ─── 真实车机 NimBLE 蓝牙数据接收路由 ───
        if (pending_data_ready) {
            current_data = pending_data;
            current_data.ble_connected = ble_adapter->is_connected();
            current_data.valid = true;
            pending_data_ready = false;
        }

        if (!current_data.valid) {
            // 还没有获得第一帧有效遥测，处于 Landing 引导阶段
            if (ble_adapter && ble_adapter->is_connected()) {
                std::vector<uint8_t> stored_key;
                bool has_key = storage_adapter && storage_adapter->load("private_key", stored_key);
                if (!has_key) {
                    // 没有配对过，展示钥匙卡片授权引导
                    static bool pairing_started = false;
                    if (!pairing_started) {
                        pairing_started = true;
                        vehicle->pair(Keys_Role_ROLE_OWNER);
                    }
                    display.show_pairing("TAP KEYCARD ON CENTER CONSOLE");
                } else {
                    // 已配对，正同步会话
                    display.show_pairing_status("BLE connected! Syncing telemetry...");
                }
            } else {
                // 寻址连接中
                display.show_pairing_status("Connecting to Vehicle (BLE)...");
            }
            
            // 跳过下面的假数据业务逻辑，继续轮询 LVGL 以驱动画面
            for (int step = 0; step < 5; step++) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }
        #else
        // ─── 本地假数据剧本连接与授权动画模拟 ───
        // 模拟开机前 4.5 秒的寻址和钥匙卡片配对授权流程
        if (loop_frame < 30) {
            current_data.valid = false;
            display.show_pairing_status("Connecting to Vehicle (BLE)...");
            for (int step = 0; step < 5; step++) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            frame_counter++;
            continue;
        } else if (loop_frame >= 30 && loop_frame < 70) {
            current_data.valid = false;
            display.show_pairing("TAP KEYCARD ON CENTER CONSOLE");
            for (int step = 0; step < 5; step++) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            frame_counter++;
            continue;
        } else if (loop_frame >= 70 && loop_frame < 90) {
            current_data.valid = false;
            display.show_pairing_status("BLE connected! Syncing telemetry...");
            for (int step = 0; step < 5; step++) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            frame_counter++;
            continue;
        }
        // 模拟已配对通过，启动正常的遥测显示
        current_data.valid = true;
        #endif

        // ─── 阶段 0：解锁上车（0.0 - 5.0秒，原0-100帧 -> 平移为 90 - 190帧） ───
        if (loop_frame >= 90 && loop_frame < 190) {
            current_data.locked = false;
            current_data.door_open_fl = true;  // 主驾门开
            current_data.door_open_rl = true;  // 左后门开
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            current_data.charging = false;
            
            // 四轮特色非对称胎压初始化
            current_data.tpms_fl = 2.4f; // 低压报警 (橘红)
            current_data.tpms_fr = 2.8f; // 正常 (白色)
            current_data.tpms_rl = 3.3f; // 高压报警 (橘红)
            current_data.tpms_rr = 2.9f; // 正常 (白色)
        }
        // ─── 阶段 1：准备起步（5.0 - 8.0秒，原100-160帧 -> 平移为 190 - 250帧） ───
        else if (loop_frame >= 190 && loop_frame < 250) {
            current_data.door_open_fl = false; // 关门
            current_data.door_open_rl = false;
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 1.5f; // 空调出风降温功率
            
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 2.8f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 2.9f;
        }
        // ─── 阶段 2：D档轻油门起步（8.0 - 12.0秒，原160-240帧 -> 平移为 250 - 330帧） ───
        else if (loop_frame >= 250 && loop_frame < 330) {
            current_data.gear = 'D'; // 挂 D 档
            if (loop_frame < 290) {
                current_data.speed_kmh = 0.0f;
                current_data.motor_power_kw = 0.0f;
            } else {
                float ratio = (loop_frame - 290) / 40.0f;
                current_data.speed_kmh = ratio * 30.0f;
                current_data.motor_power_kw = 12.0f;
            }
            if (loop_frame % 20 == 0) {
                current_data.odometer_km++;
                current_data.battery_range_km -= 0.1f;
            }
            
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 2.8f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 2.9f;
        }
        // ─── 阶段 3：强力加速超车（12.0 - 18.0秒，原240-360帧 -> 平移为 330 - 450帧） ───
        else if (loop_frame >= 330 && loop_frame < 450) {
            float ratio = (loop_frame - 330) / 120.0f;
            current_data.gear = 'D';
            current_data.speed_kmh = 30.0f + ratio * 90.0f;
            current_data.motor_power_kw = 25.0f + ratio * 60.0f;
            
            // 轮胎摩擦生热升压 (FL低压报警, FR正常升温, RL高压报警, RR由白升温变橘红)
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 2.8f + ratio * 0.3f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 2.9f + ratio * 0.4f;
            
            // 车内快速降温
            current_data.inside_temp = 35.0f - ratio * 13.0f;
            
            if (loop_frame % 15 == 0) {
                current_data.odometer_km++;
                current_data.battery_range_km -= 0.2f;
            }
        }
        // ─── 阶段 4：动能回收与重踩刹车（18.0 - 23.0秒，原360-460帧 -> 平移为 450 - 550帧） ───
        else if (loop_frame >= 450 && loop_frame < 550) {
            current_data.gear = 'D';
            if (loop_frame < 500) {
                // 松电门滑行 (120 -> 90 km/h)
                float ratio = (loop_frame - 450) / 50.0f;
                current_data.speed_kmh = 120.0f - ratio * 30.0f;
                current_data.motor_power_kw = -12.5f; // 轻度回收
            } else {
                // 重踩刹车 (90 -> 10 km/h)
                float ratio = (loop_frame - 500) / 50.0f;
                current_data.speed_kmh = 90.0f - ratio * 80.0f;
                current_data.motor_power_kw = -12.5f - ratio * 47.5f; // 强力回收
            }
            if (loop_frame % 25 == 0) {
                current_data.battery_range_km += 0.1f; // 续航微涨
            }
            
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 3.1f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 3.3f;
        }
        // ─── 阶段 5：定速巡航/辅助驾驶 SNA 激活（27.5 - 33.5秒，550 - 670帧，时长拉长为 6 秒） ───
        else if (loop_frame >= 550 && loop_frame < 670) {
            current_data.gear = '?'; // 进入 'SNA' 巡航状态，高亮 FSD 图标，但顶部 Cargear 维持高亮为物理 D 档
            current_data.locked = false;

            // 巡航速度与能耗功率动态演进曲线 (80 -> 85 -> 80 km/h)
            if (loop_frame < 570) {
                current_data.speed_kmh = 80.0f;
                current_data.motor_power_kw = 8.5f;
            } else if (loop_frame >= 570 && loop_frame < 620) {
                float r = (loop_frame - 570) / 50.0f;
                current_data.speed_kmh = 80.0f + r * 5.0f;
                current_data.motor_power_kw = 12.0f;
            } else {
                float r = (loop_frame - 620) / 50.0f;
                current_data.speed_kmh = 85.0f - r * 5.0f;
                current_data.motor_power_kw = 4.0f;
            }

            // 胎压状态
            current_data.tpms_fl = 2.4f; // 低压报警 (橘红)
            current_data.tpms_fr = 3.1f; // 正常 (白色)
            current_data.tpms_rl = 3.3f; // 高压报警 (橘红)
            current_data.tpms_rr = 3.3f; // 高压报警 (橘红)
        }
        // ─── 阶段 6：挂 R 档倒车入库（33.5 - 39.5秒，原630-710帧 -> 现为 670 - 790帧，时长拉长为 6 秒） ───
        else if (loop_frame >= 670 && loop_frame < 790) {
            current_data.gear = 'R'; // 变 R 挡，时速数字与字母 R 变红色
            if (loop_frame < 710) {
                current_data.speed_kmh = 0.0f;
                current_data.motor_power_kw = 0.0f;
            } else if (loop_frame >= 710 && loop_frame < 760) {
                float r = (loop_frame - 710) / 50.0f;
                current_data.speed_kmh = r * 6.0f; // 动态渐增到 6 km/h
                current_data.motor_power_kw = r * 5.0f;
            } else {
                float r = (loop_frame - 760) / 30.0f;
                current_data.speed_kmh = 6.0f - r * 6.0f; // 动态踩刹车减速到 0
                current_data.motor_power_kw = 4.0f - r * 4.0f;
            }
            // 胎压平稳冷却 (FL低压警示不变, FR从3.1降温到2.8白, RL高压警示不变, RR从3.3高压降温到2.9白色)
            float ratio = (loop_frame - 670) / 120.0f;
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 3.1f - ratio * 0.3f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 3.3f - ratio * 0.4f;
        }
        // ─── 阶段 7：突发开门状况（39.5 - 43.5秒，原710-790帧 -> 现为 790 - 870帧） ───
        else if (loop_frame >= 790 && loop_frame < 870) {
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            current_data.locked = false; // 解锁
            current_data.door_open_fl = true;  // 主驾驶门突然被推开！
            current_data.charging = false;
            
            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 2.8f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 2.9f;
        }
        // ─── 阶段 8：关好车门，锁车插枪充电（43.5 - 51.5秒，原790-950帧 -> 现为 870 - 1030帧） ───
        else {
            current_data.door_open_fl = false; // 门已关好
            current_data.gear = 'P';
            current_data.speed_kmh = 0.0f;
            current_data.motor_power_kw = 0.0f;
            current_data.locked = true; // 锁车
            
            // 开启充电
            current_data.charging = true;
            current_data.charge_power_kw = 7.2f;
            
            // 充电预计时间与进度条前展
            float ratio = (loop_frame - 870) / 160.0f;
            current_data.battery_level = 99 + (int)(ratio * 1.1f); // 前半段 99%，后半段跃升到 100% 以验证右对齐排版
            current_data.battery_range_km = 248.0f + ratio * 12.0f;
            current_data.minutes_to_charge_limit = 225 - (int)(ratio * 15);

            current_data.tpms_fl = 2.4f;
            current_data.tpms_fr = 2.8f;
            current_data.tpms_rl = 3.3f;
            current_data.tpms_rr = 2.9f;
        }

        // 1. 将数据直接写入 LVGL 控件，LVGL 会智能判断变化并标记 Dirty Areas
        display.render_dashboard(current_data);

        frame_counter++;
        
        // 2. 在接下来的 50ms 间隔内，每 10ms 执行一次 LVGL 轮询句柄（100Hz 刷新）
        for (int step = 0; step < 5; step++) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
