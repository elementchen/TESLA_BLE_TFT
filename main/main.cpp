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
#include "driver/uart.h"
#include "sim_payload.h"

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
// 自动继承来自 ESP-IDF sdkconfig (Kconfig) 图形配置菜单的实车/仿真切换参数
#ifdef CONFIG_TESLA_DASH_VIN
#define TESLA_VIN CONFIG_TESLA_DASH_VIN
#else
#define TESLA_VIN "LRWYGCFS2PC792568"
#endif

// ---------- 全局对象 ----------
static Display   display;
static DashData  current_data;

#define OLED_SDA   0
#define OLED_SCL   0
#define OLED_RESET 0

// ──────────────────────────────────────────────────────────────────────
// 【实车连接模块】：标准的蓝牙通信、遥测轮询和配对逻辑。
// ──────────────────────────────────────────────────────────────────────
static std::shared_ptr<BleAdapterImpl>     ble_adapter;
static std::shared_ptr<StorageAdapterImpl> storage_adapter;
static std::shared_ptr<TeslaBLE::Vehicle>  vehicle;

DashData  pending_data;
bool      pending_data_ready = false;

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
    init_tesla_ble();
    
    // 初始化串口0接收缓冲区，用于桌面串口仿真注入
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    
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

    ESP_LOGI(TAG, "Entering pure data-driven HMI status machine...");

    // 默认初始遥测数据
    current_data.valid = false;
    current_data.ble_connected = false;

    while (true) {
        // 1. 同步物理蓝牙底层连接状态
        bool is_connected = ble_adapter && ble_adapter->is_connected();
        current_data.ble_connected = is_connected;

        // 如果蓝牙意外断开，强制清除有效性，回滚到寻找连接界面
        if (!is_connected) {
            current_data.valid = false;
        }

        // 2. 串口注入自适应轮询与解析
        static uint8_t rx_buf[256];
        static int rx_len = 0;
        
        int available = 0;
        uart_get_buffered_data_len(UART_NUM_0, (size_t*)&available);
        if (available > 0) {
            int read_len = uart_read_bytes(UART_NUM_0, rx_buf + rx_len, sizeof(rx_buf) - rx_len - 1, pdMS_TO_TICKS(5));
            if (read_len > 0) {
                rx_len += read_len;
                
                // 搜索自定义仿真数据帧头 0xAA 0xBB
                for (int i = 0; i < rx_len - 1; i++) {
                    if (rx_buf[i] == 0xAA && rx_buf[i+1] == 0xBB) {
                        // 2(头) + 2(长度) + sizeof(SimPayload)(数据体) + 2(尾)
                        int total_frame_len = sizeof(SimPayload) + 6;
                        if (rx_len - i >= total_frame_len) {
                            uint16_t pay_len = 0;
                            memcpy(&pay_len, rx_buf + i + 2, 2);
                            if (pay_len == sizeof(SimPayload)) {
                                // 验证帧尾 0xCC 0xDD
                                int tail_pos = i + 4 + sizeof(SimPayload);
                                if (rx_buf[tail_pos] == 0xCC && rx_buf[tail_pos + 1] == 0xDD) {
                                    const SimPayload *sim = reinterpret_cast<const SimPayload*>(rx_buf + i + 4);
                                    
                                    pending_data.speed_kmh = sim->speed_kmh;
                                    pending_data.motor_power_kw = sim->motor_power_kw;
                                    pending_data.battery_level = sim->battery_level;
                                    pending_data.battery_range_km = sim->battery_range_km;
                                    pending_data.gear = sim->gear;
                                    
                                    pending_data.door_open_fl = sim->doors[0];
                                    pending_data.door_open_fr = sim->doors[1];
                                    pending_data.door_open_rl = sim->doors[2];
                                    pending_data.door_open_rr = sim->doors[3];
                                    pending_data.door_open_trunk_front = sim->doors[4];
                                    pending_data.door_open_trunk_rear = sim->doors[5];
                                    
                                    pending_data.locked = sim->locked;
                                    pending_data.charging = sim->charging;
                                    pending_data.charge_power_kw = sim->charge_power_kw;
                                    pending_data.inside_temp = sim->inside_temp;
                                    pending_data.outside_temp = sim->outside_temp;
                                    
                                    pending_data.tpms_fl = sim->tpms[0];
                                    pending_data.tpms_fr = sim->tpms[1];
                                    pending_data.tpms_rl = sim->tpms[2];
                                    pending_data.tpms_rr = sim->tpms[3];
                                    
                                    pending_data.valid = true;
                                    pending_data_ready = true;
                                }
                            }
                            // 丢弃已经处理的帧，对齐缓冲区
                            memmove(rx_buf, rx_buf + i + total_frame_len, rx_len - (i + total_frame_len));
                            rx_len -= (i + total_frame_len);
                            break;
                        }
                    }
                }
                // 防止溢出
                if (rx_len >= sizeof(rx_buf) - 2) {
                    rx_len = 0;
                }
            }
        }

        // 3. 真实遥测数据包接收路由
        if (pending_data_ready) {
            current_data = pending_data;
            current_data.ble_connected = is_connected;
            current_data.valid = true;
            pending_data_ready = false;
        }

        // 3. 根据真实状态和数据驱动屏幕状态
        if (!current_data.valid) {
            if (is_connected) {
                std::vector<uint8_t> stored_key;
                bool has_key = storage_adapter && storage_adapter->load("private_key", stored_key);
                if (!has_key) {
                    // 未配对，提示卡片授权
                    static bool pairing_started = false;
                    if (!pairing_started) {
                        pairing_started = true;
                        vehicle->pair(Keys_Role_ROLE_OWNER);
                    }
                    display.show_pairing("TAP KEYCARD ON CENTER CONSOLE");
                } else {
                    // 已配对，密钥会话建立中
                    display.show_pairing_status("BLE connected! Syncing telemetry...");
                }
            } else {
                // 蓝牙寻找连接中
                display.show_pairing_status("Connecting to Vehicle (BLE)...");
            }
        } else {
            // 遥测数据生效，渲染行驶仪表主屏
            display.render_dashboard(current_data);
        }

        // 4. 执行 LVGL 轮询句柄（100Hz 刷新渲染）
        for (int step = 0; step < 5; step++) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
