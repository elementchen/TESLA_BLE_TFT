#include <cstdio>
#include <string>
#include <memory>
#include <atomic>
#include <cmath>
#include "driver/usb_serial_jtag.h"
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
#include "config_manager.h"
#include "lvgl.h"

static constexpr const char *TAG = "TeslaDash";

// ---------- 全局对象 ----------
static Display   display;
static DashData  current_data;

// ──────────────────────────────────────────────────────────────────────
// 【实车连接模块】：标准的蓝牙通信、遥测轮询和配对逻辑。
// ──────────────────────────────────────────────────────────────────────
static std::shared_ptr<BleAdapterImpl>     ble_adapter;
static std::shared_ptr<StorageAdapterImpl> storage_adapter;
static std::shared_ptr<TeslaBLE::Vehicle>  vehicle;
static PollScheduler                       scheduler;

// TPMS fault-triggered quiet mode
static bool tpms_fault_detected = false;

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
        pending_data.speed_kmh = mph_to_kmh(ds.optional_speed_float.speed_float);
    } else if (ds.which_optional_speed != 0) {
        pending_data.speed_kmh = mph_to_kmh(static_cast<float>(ds.optional_speed.speed));
    }
    pending_data.gear = gear_from_shift_state(ds.shift_state);
    if (ds.which_optional_odometer_in_hundredths_of_a_mile != 0) {
        pending_data.odometer_km = hundredths_mile_to_km(
            ds.optional_odometer_in_hundredths_of_a_mile.odometer_in_hundredths_of_a_mile);
    }
    if (ds.which_optional_power != 0) {
        pending_data.motor_power_kw = static_cast<float>(ds.optional_power.power);
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

    // Detect TPMS sensor communication faults (car can't read sensors)
    bool any_warning = false;
    if (tp.which_optional_tpms_soft_warning_fl && tp.optional_tpms_soft_warning_fl.tpms_soft_warning_fl) any_warning = true;
    if (tp.which_optional_tpms_soft_warning_fr && tp.optional_tpms_soft_warning_fr.tpms_soft_warning_fr) any_warning = true;
    if (tp.which_optional_tpms_soft_warning_rl && tp.optional_tpms_soft_warning_rl.tpms_soft_warning_rl) any_warning = true;
    if (tp.which_optional_tpms_soft_warning_rr && tp.optional_tpms_soft_warning_rr.tpms_soft_warning_rr) any_warning = true;
    if (tp.which_optional_tpms_hard_warning_fl && tp.optional_tpms_hard_warning_fl.tpms_hard_warning_fl) any_warning = true;
    if (tp.which_optional_tpms_hard_warning_fr && tp.optional_tpms_hard_warning_fr.tpms_hard_warning_fr) any_warning = true;
    if (tp.which_optional_tpms_hard_warning_rl && tp.optional_tpms_hard_warning_rl.tpms_hard_warning_rl) any_warning = true;
    if (tp.which_optional_tpms_hard_warning_rr && tp.optional_tpms_hard_warning_rr.tpms_hard_warning_rr) any_warning = true;
    if (any_warning) tpms_fault_detected = true;

    pending_data_ready.store(true, std::memory_order_release);
    last_tpms_tick = xTaskGetTickCount();
}

static void on_vehicle_status(const VCSEC_VehicleStatus &status) {
    bool awake = (status.vehicleSleepStatus
                  == VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE);
    current_data.vehicle_awake = awake;

    // 从 VCSEC 快速通道（~320ms）提取车门/锁状态
    // ClosureState_E: 0=CLOSED → door_open=false; 其他(OPEN/AJAR/UNKNOWN等)=true
    if (status.has_closureStatuses) {
        auto &cs = status.closureStatuses;
        current_data.door_open_fl = (cs.frontDriverDoor    != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
        current_data.door_open_fr = (cs.frontPassengerDoor != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
        current_data.door_open_rl = (cs.rearDriverDoor     != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
        current_data.door_open_rr = (cs.rearPassengerDoor  != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
        current_data.door_open_trunk_front = (cs.frontTrunk != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
        current_data.door_open_trunk_rear  = (cs.rearTrunk  != VCSEC_ClosureState_E_CLOSURESTATE_CLOSED);
    }

    // vehicleLockState: 0=UNLOCKED, 1及以上=LOCKED/INTERNAL_LOCKED等
    current_data.locked = (status.vehicleLockState != VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_UNLOCKED);
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
    vehicle->set_vin(config_get_vin());
    vehicle->set_drive_state_callback(on_drive_state);
    vehicle->set_charge_state_callback(on_charge_state);
    vehicle->set_climate_state_callback(on_climate_state);
    vehicle->set_closures_state_callback(on_closures_state);
    vehicle->set_tire_pressure_state_callback(on_tire_pressure);
    vehicle->set_vehicle_status_callback(on_vehicle_status);
    
    // 必须调用初始化，以启动 NimBLE 任务栈和蓝牙广播扫描
    ble_adapter->init(config_get_vin());

    // ─── 场景驱动轮询调度器 ──────────────────────────────────
    // register_poll(name, priority, DRIVING_ms, DOOR_OPEN_ms, CHARGING_ms, CONNECTING_ms, lambda)
    // 0 = disabled in that mode. 车机每 ~800ms 消化一个命令，所以同时活跃 ≤2 种类型。

    // DRIVING: 仅 drive_state 独占 — 时速/档位/功率零拥堵
    //   closures 仅 P 档时动态启用（见主循环 set_slot_enabled）
    // DOOR_OPEN: 仅 closures @ 500ms — 最快检测关门
    // CHARGING: 仅 charge @ 500ms — 功率/SOC 实时
    // CONNECTING: vcsec + drive + charge @ 2000ms — 会话维持 + 首帧数据
    scheduler.register_poll("drive",    PollPriority::HIGH,   800,    0,    0, 2000,
        []() { if (vehicle) vehicle->drive_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("closures", PollPriority::HIGH,   500,  500, 60000,    0,
        []() { if (vehicle) vehicle->closures_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("charge",   PollPriority::MEDIUM, 120000,  0,  500, 2000,
        []() { if (vehicle) vehicle->charge_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("vcsec",    PollPriority::MEDIUM, 5000,   0, 5000, 2000,
        []() { if (vehicle) vehicle->vcsec_poll(); });
    scheduler.register_poll("climate",  PollPriority::LOW,   900000,   0,    0,    0,
        []() { if (vehicle) vehicle->climate_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });
    scheduler.register_poll("tpms",     PollPriority::BACKGROUND, 300000, 0, 0, 0,
        []() { if (vehicle) vehicle->tire_pressure_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP); });

    // 初始模式 — 必须在注册后显式调用以启用对应 slot
    scheduler.set_mode(DashMode::CONNECTING);
}

// ---------- 初始化显示屏幕 ----------
static void init_display() {
    DisplayPins pins = config_get_display_pins();
    if (!display.init(pins)) {
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
    ESP_LOGI(TAG, "=== Tesla BLE Dashboard v2.0 ===");

    // 初始化 USB Serial JTAG 驱动（非阻塞读取配置命令）
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb_cfg);

    config_manager_init();
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
        // ─── Cat-1 启动阶段状态机 ──────────────────────────────
        // STARTUP → 检查 NVS → 有密钥 → SYNC / 无密钥 → PAIRING
        // SYNC    → 等连接+数据 → 成功 → session_established
        //         → 超时(25s)  → 删密钥 → PAIRING
        // PAIRING → 等刷卡授权 → 成功 → session_established
        //         → 永不超时（用户可能不在车内）
        enum class Cat1Phase : uint8_t { STARTUP, SYNC, PAIRING };
        static Cat1Phase cat1 = Cat1Phase::STARTUP;
        static uint32_t sync_start_tick = 0;
        static bool cat1_pair_sent = false;

        // session_established: 一旦首次收到有效遥测数据即置 true，
        // 之后永不回退 Category-1 界面，蓝牙波动仅通过 BLE 小圆点反馈。
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
                pending_data_ready.store(false, std::memory_order_release);
                session_established = false;
                cat1 = Cat1Phase::PAIRING;
                cat1_pair_sent = false;
                sync_start_tick = 0;
            }

            // ─── 场景驱动轮询调度 ──────────────────────────
            {
                uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

                // Detect dashboard mode from current telemetry
                DashMode new_mode = DashMode::CONNECTING;
                if (session_established) {
                    bool any_door = (current_data.door_open_fl || current_data.door_open_fr ||
                                     current_data.door_open_rl || current_data.door_open_rr ||
                                     current_data.door_open_trunk_front || current_data.door_open_trunk_rear);
                    if (any_door) {
                        new_mode = DashMode::DOOR_OPEN;
                    } else if (current_data.charging) {
                        new_mode = DashMode::CHARGING;
                    } else {
                        new_mode = DashMode::DRIVING;
                    }
                }
                if (new_mode != scheduler.get_mode()) {
                    scheduler.set_mode(new_mode);
                }

                // ─── BLE 静默窗口（TPMS 共存）──────────────────────
                // 周期性: 1s 静默 / 10s (对齐 Continental BLE TPMS 10s 唤醒周期)
                // 触发式: 检测到 TPMS warning → 10s 完整周期静默 + 相位重置
                {
                    static uint32_t quiet_phase = 0;
                    static bool in_quiet = false;
                    static uint32_t fault_until = 0;
                    constexpr uint32_t CYCLE_MS  = 7200;   // 8 polls × 800ms + 800ms quiet
                    constexpr uint32_t WINDOW_MS = 800;    // one poll slot = 800ms
                    constexpr uint32_t FAULT_MS  = 10000;  // full TPMS cycle on warning
                    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

                    // TPMS warning → full-cycle quiet + phase reset
                    if (tpms_fault_detected) {
                        tpms_fault_detected = false;
                        fault_until = now + FAULT_MS;
                        scheduler.set_quiet(true);
                        ESP_LOGW(TAG, "TPMS fault — 10s quiet + phase reset");
                    }
                    if (fault_until && now > fault_until) {
                        fault_until = 0;
                        scheduler.set_quiet(false);
                        in_quiet = false;
                        quiet_phase = now;     // phase reset
                    }

                    // Periodic quiet (suppressed during fault window)
                    if (!fault_until) {
                        if (!in_quiet) {
                            if (quiet_phase == 0 || now - quiet_phase > CYCLE_MS) {
                                in_quiet = true;
                                quiet_phase = now;
                                scheduler.set_quiet(true);
                            }
                        } else {
                            if (now - quiet_phase > WINDOW_MS) {
                                in_quiet = false;
                                quiet_phase = now;
                                scheduler.set_quiet(false);
                            }
                        }
                    }
                }

                // 仅 P 档检测车门；D/R/N/? 时带宽全给时速/档位
                if (new_mode == DashMode::DRIVING) {
                    bool need_doors = (current_data.gear == 'P');
                    scheduler.set_slot_enabled("closures", need_doors);

                    // TPMS: 仅前2次读取（首次+5分钟后一次），之后停至下次P档
                    // 减少蓝牙负载，避免干扰车机-轮胎传感器通信
                    static int tpms_polls_since_park = 0;
                    static char last_gear_tpms = 'P';
                    if (current_data.gear == 'P') {
                        tpms_polls_since_park = 0;
                        scheduler.set_slot_enabled("tpms", true);
                    } else if (current_data.gear != last_gear_tpms) {
                        // 刚离开P档（开始驾驶），允许前2次
                        tpms_polls_since_park = 0;
                        scheduler.set_slot_enabled("tpms", true);
                    } else {
                        // 行驶中：跟踪已轮询次数
                        static uint32_t last_tpms_tick_count = 0;
                        if (last_tpms_tick != last_tpms_tick_count) {
                            last_tpms_tick_count = last_tpms_tick;
                            tpms_polls_since_park++;
                            if (tpms_polls_since_park >= 2) {
                                scheduler.set_slot_enabled("tpms", false);
                                ESP_LOGI(TAG, "TPMS: 2 polls done, disabled until next P");
                            }
                        }
                    }
                    last_gear_tpms = current_data.gear;
                }

                size_t qdepth = vehicle->get_command_queue_depth();
                g_diag.record_queue_depth(qdepth);

                scheduler.process(now_ms, qdepth,
                                  TeslaBLE::Vehicle::MAX_COMMAND_QUEUE_SIZE);

                // ─── 诊断摘要（每 60 秒） ──────────────────────
                g_diag.print_summary_if_due(now_ms);
            }
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
        bool drive_stale = false;
        if (is_connected && current_data.drive_state_update_tick != 0) {
            uint32_t drive_age = xTaskGetTickCount() - current_data.drive_state_update_tick;
            drive_stale = (drive_age > pdMS_TO_TICKS(DashData::STALE_DRIVE_STATE_MS));
        }
        bool data_healthy = current_data.valid && is_connected && !drive_stale;
        current_data.ble_connected = data_healthy;

        // ─── 真实数据到达 → session_established ─────────────────
        if (current_data.valid && !session_established) {
            bool has_real_data = (current_data.battery_level > 0) ||
                                 (current_data.odometer_km > 0) ||
                                 (current_data.inside_temp != 0.0f) ||
                                 (current_data.outside_temp != 0.0f);
            if (has_real_data) {
                session_established = true;
                ESP_LOGI(TAG, "Real vehicle data confirmed — locking to Cat-2 screens");
            }
        }

        // 4. 屏幕状态机
        //    Cat-1: Landing(1) / Card Pair(2) / Sync(3)
        //    Cat-2: Drive / Charge / Door Open
        if (!session_established) {
            // ── Cat-1 启动阶段 ──────────────────────────────────
            switch (cat1) {
            case Cat1Phase::STARTUP: {
                // 界面 1 — 决策点：有历史密钥 → SYNC / 无密钥 → PAIRING
                std::vector<uint8_t> stored_key;
                bool has_key = storage_adapter && storage_adapter->load("private_key", stored_key);
                if (has_key) {
                    cat1 = Cat1Phase::SYNC;
                    sync_start_tick = 0;
                    ESP_LOGI(TAG, "STARTUP: key found (%zu bytes) → SYNC", stored_key.size());
                } else {
                    cat1 = Cat1Phase::PAIRING;
                    cat1_pair_sent = false;
                    ESP_LOGI(TAG, "STARTUP: no key → PAIRING");
                }
                break;
            }
            case Cat1Phase::SYNC: {
                // 界面 1↔3：有密钥，等待连接+同步
                static uint32_t last_sync_log_tick = 0;
                if (is_connected) {
                    if (sync_start_tick == 0) {
                        sync_start_tick = xTaskGetTickCount();
                    }
                    // 每 5 秒输出一次诊断
                    if (xTaskGetTickCount() - last_sync_log_tick > pdMS_TO_TICKS(5000)) {
                        last_sync_log_tick = xTaskGetTickCount();
                        ESP_LOGI(TAG, "SYNC: connected=%d valid=%d qdepth=%zu",
                                 is_connected, current_data.valid,
                                 vehicle ? vehicle->get_command_queue_depth() : 0);
                    }
                    static constexpr uint32_t SYNC_TIMEOUT_MS = 25000;
                    if (xTaskGetTickCount() - sync_start_tick > pdMS_TO_TICKS(SYNC_TIMEOUT_MS)) {
                        ESP_LOGW(TAG, "SYNC timeout (%" PRIu32 "s) — erasing keys, falling back to PAIRING",
                                 SYNC_TIMEOUT_MS / 1000);
                        if (storage_adapter) {
                            storage_adapter->remove("private_key");
                            storage_adapter->remove("public_key");
                        }
                        if (vehicle) vehicle->clear_commands();
                        current_data.valid = false;
                        cat1 = Cat1Phase::PAIRING;
                        cat1_pair_sent = false;
                        sync_start_tick = 0;
                    } else {
                        display.show_pairing_status("Syncing...");
                    }
                } else {
                    display.show_pairing_status("System Startup");
                    sync_start_tick = 0;
                }
                break;
            }
            case Cat1Phase::PAIRING: {
                // 界面 2：无密钥，等待刷卡授权
                if (!cat1_pair_sent) {
                    cat1_pair_sent = true;
                    vehicle->pair(Keys_Role_ROLE_OWNER);
                    ESP_LOGI(TAG, "Pairing request sent — waiting for keycard tap");
                }
                // 根据 BLE 连接状态更新 UI 提示文字
                if (cat1_pair_sent) {
                    if (is_connected) {
                        display.show_pairing("Connecting...");
                    } else {
                        display.show_pairing("TAP CARD");
                    }
                }
                break;
            }
            }
        } else {
            // ── Cat-2 正常运行（永不回退）───────────────────────
            display.render_dashboard(current_data);
            sync_start_tick = 0;
        }

        // 4. 处理 USB 串口配置命令（非阻塞）
        {
            static std::string serial_buf;
            uint8_t buf[64];
            int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0); if (n > 0) ESP_LOGI(TAG, "SerialRX: %d bytes", n);
            for (int i = 0; i < n; i++) {
                char ch = (char)buf[i];
                if (ch == '\n' || ch == '\r') {
                    if (!serial_buf.empty()) {
                        config_manager_process_command(serial_buf);
                        serial_buf.clear();
                    }
                } else if (ch >= 0x20 && ch <= 0x7E) {
                    serial_buf += ch;
                }
            }
        }

        // 5. 执行 LVGL 轮询句柄（100Hz 刷新渲染）
        for (int step = 0; step < 5; step++) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
