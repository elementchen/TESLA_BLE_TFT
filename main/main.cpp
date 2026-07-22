#include <cstdio>
#include <string>
#include <memory>
#include <atomic>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "sim_payload.h"

#include "vehicle.h"
#include "car_server.pb.h"
#include "vcsec.pb.h"

#include "ble_adapter.h"
#include "storage_adapter.h"
#include "display.h"
#include "dash_data.h"
#include "poll_scheduler.h"
#include "diagnostics.h"
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
static PollScheduler                       scheduler;

DashData           pending_data;
std::atomic<bool>  pending_data_ready{false};

// Per-type last-update ticks for staleness detection
static uint32_t last_drive_state_tick    = 0;
static uint32_t last_charge_state_tick   = 0;
static uint32_t last_climate_state_tick  = 0;
static uint32_t last_closures_state_tick = 0;
static uint32_t last_tpms_tick           = 0;

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
    pending_data_ready.store(true, std::memory_order_release);
    last_drive_state_tick = xTaskGetTickCount();
}

static void on_charge_state(const CarServer_ChargeState &cs) {
    if (cs.which_optional_battery_level)
        pending_data.battery_level = cs.optional_battery_level.battery_level;
    if (cs.which_optional_battery_range)
        pending_data.battery_range_km = mph_to_kmh(cs.optional_battery_range.battery_range);
    if (cs.which_optional_charge_limit_soc)
        pending_data.charge_limit_soc = cs.optional_charge_limit_soc.charge_limit_soc;
    if (cs.which_optional_charger_power)
        pending_data.charge_power_kw = static_cast<float>(cs.optional_charger_power.charger_power);
    if (cs.which_optional_minutes_to_full_charge) {
        // prefer minutes_to_charge_limit (set by user); fallback to full charge
    }
    if (cs.which_optional_minutes_to_charge_limit)
        pending_data.minutes_to_charge_limit = cs.optional_minutes_to_charge_limit.minutes_to_charge_limit;
    // charging_state is a oneof sub-message with which_type + union
    switch (cs.charging_state.which_type) {
        case CarServer_ChargeState_ChargingState_Charging_tag:
            pending_data.charging = true;
            pending_data.charging_state_str = "Charging";
            break;
        case CarServer_ChargeState_ChargingState_Complete_tag:
            pending_data.charging = false;
            pending_data.charging_state_str = "Complete";
            break;
        case CarServer_ChargeState_ChargingState_Stopped_tag:
            pending_data.charging = false;
            pending_data.charging_state_str = "Stopped";
            break;
        case CarServer_ChargeState_ChargingState_NoPower_tag:
            pending_data.charging = false;
            pending_data.charging_state_str = "NoPower";
            break;
        case CarServer_ChargeState_ChargingState_Disconnected_tag:
        default:
            pending_data.charging = false;
            pending_data.charging_state_str = "Disconnected";
            break;
    }
    pending_data_ready.store(true, std::memory_order_release);
    last_charge_state_tick = xTaskGetTickCount();
}

static void on_climate_state(const CarServer_ClimateState &cs) {
    if (cs.which_optional_inside_temp_celsius)
        pending_data.inside_temp = cs.optional_inside_temp_celsius.inside_temp_celsius;
    if (cs.which_optional_outside_temp_celsius)
        pending_data.outside_temp = cs.optional_outside_temp_celsius.outside_temp_celsius;
    pending_data_ready.store(true, std::memory_order_release);
    last_climate_state_tick = xTaskGetTickCount();
}

static void on_closures_state(const CarServer_ClosuresState &cs) {
    if (cs.which_optional_locked)
        pending_data.locked = cs.optional_locked.locked;
    if (cs.which_optional_door_open_driver_front)
        pending_data.door_open_fl = cs.optional_door_open_driver_front.door_open_driver_front;
    if (cs.which_optional_door_open_passenger_front)
        pending_data.door_open_fr = cs.optional_door_open_passenger_front.door_open_passenger_front;
    if (cs.which_optional_door_open_driver_rear)
        pending_data.door_open_rl = cs.optional_door_open_driver_rear.door_open_driver_rear;
    if (cs.which_optional_door_open_passenger_rear)
        pending_data.door_open_rr = cs.optional_door_open_passenger_rear.door_open_passenger_rear;
    if (cs.which_optional_door_open_trunk_front)
        pending_data.door_open_trunk_front = cs.optional_door_open_trunk_front.door_open_trunk_front;
    if (cs.which_optional_door_open_trunk_rear)
        pending_data.door_open_trunk_rear = cs.optional_door_open_trunk_rear.door_open_trunk_rear;
    pending_data_ready.store(true, std::memory_order_release);
    last_closures_state_tick = xTaskGetTickCount();
}

static void on_tire_pressure(const CarServer_TirePressureState &tp) {
    if (tp.which_optional_tpms_pressure_fl)
        pending_data.tpms_fl = tp.optional_tpms_pressure_fl.tpms_pressure_fl;
    if (tp.which_optional_tpms_pressure_fr)
        pending_data.tpms_fr = tp.optional_tpms_pressure_fr.tpms_pressure_fr;
    if (tp.which_optional_tpms_pressure_rl)
        pending_data.tpms_rl = tp.optional_tpms_pressure_rl.tpms_pressure_rl;
    if (tp.which_optional_tpms_pressure_rr)
        pending_data.tpms_rr = tp.optional_tpms_pressure_rr.tpms_pressure_rr;
    pending_data_ready.store(true, std::memory_order_release);
    last_tpms_tick = xTaskGetTickCount();
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
        if (!connected) g_diag.record_disconnect();
        else            g_diag.record_reconnect();
        current_data.ble_connected = connected;
        if (!connected) current_data.valid = false;
    });
    vehicle = std::make_shared<TeslaBLE::Vehicle>(ble_adapter, storage_adapter);
    vehicle->set_vin(TESLA_VIN);
    vehicle->set_drive_state_callback(on_drive_state);
    vehicle->set_charge_state_callback(on_charge_state);
    vehicle->set_climate_state_callback(on_climate_state);
    vehicle->set_closures_state_callback(on_closures_state);
    vehicle->set_tire_pressure_state_callback(on_tire_pressure);
    vehicle->set_vehicle_status_callback(on_vehicle_status);
    
    // 必须调用初始化，以启动 NimBLE 任务栈和蓝牙广播扫描
    ble_adapter->init(TESLA_VIN);

    // ─── 注册优先级轮询调度器 ───────────────────────────────────
    // 间隔参数: (名称, 优先级, 停车间隔ms, 行驶倍率, lambda)
    // HIGH: speed/gear — 500ms parked, 250ms moving (*0.5)
    scheduler.register_poll("drive", PollPriority::HIGH, 500, 0.5f,
        []() { if (vehicle) vehicle->drive_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    // MEDIUM: battery/charge/VCSEC — 1s always
    scheduler.register_poll("charge", PollPriority::MEDIUM, 1000, 1.0f,
        []() { if (vehicle) vehicle->charge_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("vcsec", PollPriority::MEDIUM, 1000, 1.0f,
        []() { if (vehicle) vehicle->vcsec_poll(); });
    // LOW: climate/closures — 3s parked, 6s moving (*2.0)
    scheduler.register_poll("climate", PollPriority::LOW, 3000, 2.0f,
        []() { if (vehicle) vehicle->climate_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("closures", PollPriority::LOW, 3000, 2.0f,
        []() { if (vehicle) vehicle->closures_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    // BACKGROUND: tire pressure — 10s parked, 20s moving (*2.0)
    scheduler.register_poll("tpms", PollPriority::BACKGROUND, 10000, 2.0f,
        []() { if (vehicle) vehicle->tire_pressure_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
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
        // ─── 状态变量（static 保持在栈上） ──────────────────────
        static uint32_t sync_start_tick = 0;
        static bool pairing_started = false;

        // session_established: 一旦首次收到有效遥测数据即置 true，
        // 之后永不回退 Category-1 界面（Landing/Pair/Sync），
        // 蓝牙波动仅通过顶部 BLE 状态小圆点颜色（蓝/红）反馈。
        static bool session_established = false;

        // 1. 驱动底层 BLE 适配器（处理延迟探索与数据包）
        if (ble_adapter) {
            ble_adapter->process();
        }

        // 2. 驱动特斯拉实车协议层状态机（处理 VCSEC/CarServer 密文握手与遥测轮询）
        if (vehicle) {
            vehicle->loop();

            // ─── 密钥撤销实时检测 ─────────────────────────────────
            if (vehicle->is_key_revoked()) {
                ESP_LOGW(TAG, "Key revoked by vehicle — erasing keys, restarting pairing");
                if (storage_adapter) {
                    storage_adapter->remove("private_key");
                    storage_adapter->remove("public_key");
                }
                vehicle->clear_key_revoked_flag();
                vehicle->clear_commands();
                current_data.valid = false;
                session_established = false;
                pairing_started = false;
                sync_start_tick = 0;
            }

            // ─── 优先级轮询调度: 每次最多发1个请求，仅在队列有空时 ───
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            scheduler.set_vehicle_moving(current_data.gear != 'P' && current_data.gear != 0);

            size_t qdepth = vehicle->get_command_queue_depth();
            g_diag.record_queue_depth(qdepth);

            scheduler.process(now_ms, qdepth,
                              TeslaBLE::Vehicle::MAX_COMMAND_QUEUE_SIZE);

            // ─── 诊断摘要（每 60 秒） ──────────────────────────
            g_diag.print_summary_if_due(now_ms);
        }

        // 3. 物理蓝牙连接状态与遥测心跳检测
        bool is_connected = ble_adapter && ble_adapter->is_connected();
        current_data.rssi = ble_adapter ? ble_adapter->get_rssi() : -128;

        // 真实遥测数据包接收路由
        if (pending_data_ready.load(std::memory_order_acquire)) {
            current_data = pending_data;
            current_data.ble_connected = is_connected;
            current_data.valid = true;
            // Copy per-type age ticks (preserve previous if not updated this cycle)
            if (last_drive_state_tick)    current_data.drive_state_update_tick    = last_drive_state_tick;
            if (last_charge_state_tick)   current_data.charge_state_update_tick   = last_charge_state_tick;
            if (last_climate_state_tick)  current_data.climate_state_update_tick  = last_climate_state_tick;
            if (last_closures_state_tick) current_data.closures_state_update_tick = last_closures_state_tick;
            if (last_tpms_tick)           current_data.tire_pressure_update_tick  = last_tpms_tick;
            pending_data_ready.store(false, std::memory_order_release);
            sync_start_tick = 0; // 遥测数据生效，重置同步定时器
        }

        // ─── 分类型遥测超时判定 ──────────────────────────────────
        // 检测数据是否陈旧，但 session 建立后绝不回退 Category-1 界面。
        bool drive_stale = false;
        if (is_connected && current_data.drive_state_update_tick != 0) {
            uint32_t drive_age = xTaskGetTickCount() - current_data.drive_state_update_tick;
            drive_stale = (drive_age > pdMS_TO_TICKS(DashData::STALE_DRIVE_STATE_MS));
        }
        bool data_healthy = current_data.valid && is_connected && !drive_stale;

        if (current_data.valid && !session_established) {
            // 确认收到来自车机的真实数据（里程/温度不可能为 0 或空）
            bool has_real_data = (current_data.odometer_km > 0) ||
                                 (current_data.inside_temp != 0.0f) ||
                                 (current_data.outside_temp != 0.0f);
            if (has_real_data) {
                session_established = true;
                ESP_LOGI(TAG, "Real vehicle data confirmed (odo=%" PRIu32 "km, in=%.1fC, out=%.1fC) "
                         "— locking to Category-2 screens",
                         current_data.odometer_km,
                         current_data.inside_temp, current_data.outside_temp);
            }
        }

        // ─── 更新 BLE 连接状态（在 Category-2 界面上仅通过小圆点体现）───
        current_data.ble_connected = data_healthy;

        // 4. 根据当前真实状态驱动 UI 屏幕切换
        // ─── 两分类屏幕模型 ─────────────────────────────────────
        // Cat-1（仅开机/重启时出现）: Landing/Pair/Sync
        // Cat-2（session 建立后只在这三个中切换）: Drive/Charge/DoorOpen
        if (!session_established) {
            // ── 启动阶段：Category-1 界面 ──────────────────────
            if (!is_connected) {
                display.show_pairing_status("Connecting to Vehicle (BLE)...");
                pairing_started = false;
                sync_start_tick = 0;
            } else if (current_data.valid) {
                // 遥测已通 → 下一帧即标记 session_established 并进入 Cat-2
                display.render_dashboard(current_data);
                sync_start_tick = 0;
            } else {
                // 已连接但无数据：配对或同步中
                std::vector<uint8_t> stored_key;
                bool has_key = storage_adapter && storage_adapter->load("private_key", stored_key);
                if (!has_key) {
                    if (!pairing_started) {
                        pairing_started = true;
                        vehicle->pair(Keys_Role_ROLE_OWNER);
                    }
                    display.show_pairing("TAP KEYCARD ON CENTER CONSOLE");
                    sync_start_tick = 0;
                } else {
                    if (sync_start_tick == 0) {
                        sync_start_tick = xTaskGetTickCount();
                    }
                    if (xTaskGetTickCount() - sync_start_tick > pdMS_TO_TICKS(8000)) {
                        ESP_LOGW(TAG, "Session sync timeout or key rejected! Erasing stale keys...");
                        if (storage_adapter) {
                            storage_adapter->remove("private_key");
                            storage_adapter->remove("public_key");
                        }
                        if (vehicle) vehicle->clear_commands();
                        pairing_started = false;
                        sync_start_tick = 0;
                    } else {
                        display.show_pairing_status("BLE connected! Syncing telemetry...");
                    }
                }
            }
        } else {
            // ── 正常运行：Category-2 界面（Drive/Charge/DoorOpen）───
            // 无论蓝牙是否波动、数据是否陈旧，始终渲染仪表盘。
            // 连接状态仅通过顶部 Cargear 组件的 BLE 小圆点颜色体现。
            display.render_dashboard(current_data);
            sync_start_tick = 0;
            pairing_started = false;
        }

        // 4. 执行 LVGL 轮询句柄（100Hz 刷新渲染）
        for (int step = 0; step < 5; step++) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
