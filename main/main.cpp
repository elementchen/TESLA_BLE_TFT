#include <cstdio>
#include <string>
#include <memory>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "vehicle.h"
#include "car_server.pb.h"
#include "vcsec.pb.h"

#include "ble_adapter.h"
#include "storage_adapter.h"
#include "display.h"
#include "dash_data.h"

static constexpr const char *TAG = "TeslaDash";

// ---------- 配置 ----------
// 修改为你的 Tesla VIN（17位）
#ifndef TESLA_VIN
#define TESLA_VIN "LRWYGCFS2PC792568"
#endif

// DriveState 轮询间隔（行车中 vs 停车）
static constexpr int POLL_INTERVAL_ACTIVE_MS = 2000;
static constexpr int POLL_INTERVAL_IDLE_MS  = 10000;

// ---------- 全局对象 ----------
static std::shared_ptr<BleAdapterImpl>     ble_adapter;
static std::shared_ptr<StorageAdapterImpl> storage_adapter;
static std::shared_ptr<TeslaBLE::Vehicle>  vehicle;

static Display   display;
static DashData  current_data;
static DashData  pending_data;
static bool      pending_data_ready = false;
static TickType_t last_poll_time = 0;
static bool      paired = false;

// OLED I2C 引脚（通过 sdkconfig 可配置）
#define OLED_SDA   CONFIG_OLED_SDA_GPIO
#define OLED_SCL   CONFIG_OLED_SCL_GPIO
#define OLED_RESET CONFIG_OLED_RESET_GPIO

// Pairing reset button: long-press GPIO4 for 5s to clear keys and re-pair
#define PAIR_RESET_GPIO GPIO_NUM_4
static constexpr int PAIR_RESET_HOLD_MS = 5000;

// ---------- 档位转换 ----------
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

// ---------- DriveState 回调 ----------
static void on_drive_state(const CarServer_DriveState &ds) {
    // speed_float（优先 float 版本，更精确）
    if (ds.which_optional_speed_float != 0) {
        pending_data.speed_kmh = ds.optional_speed_float.speed_float;
    } else if (ds.which_optional_speed != 0) {
        pending_data.speed_kmh = static_cast<float>(ds.optional_speed.speed);
    }

    // 档位
    pending_data.gear = gear_from_shift_state(ds.shift_state);

    // 里程（hundredths of a mile → km）
    if (ds.which_optional_odometer_in_hundredths_of_a_mile != 0) {
        pending_data.odometer_km = hundredths_mile_to_km(
            ds.optional_odometer_in_hundredths_of_a_mile.odometer_in_hundredths_of_a_mile);
    }

    pending_data.valid = true;
    pending_data_ready = true;

    ESP_LOGD(TAG, "DriveState: speed=%.1f km/h, gear=%c, odo=%u km",
             pending_data.speed_kmh, pending_data.gear,
             (unsigned int)pending_data.odometer_km);
}

// ---------- 车辆状态回调 ----------
static void on_vehicle_status(const VCSEC_VehicleStatus &status) {
    bool awake = (status.vehicleSleepStatus
                  == VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE);
    current_data.vehicle_awake = awake;
    ESP_LOGD(TAG, "Vehicle: %s", awake ? "AWAKE" : "ASLEEP");
}

// ---------- 初始化 ----------
static void init_display() {
    if (!display.init(OLED_SDA, OLED_SCL, OLED_RESET)) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    display.show_splash();
    ESP_LOGI(TAG, "Display ready");
}

static void init_tesla_ble() {
    storage_adapter = std::make_shared<StorageAdapterImpl>();
    ble_adapter     = std::make_shared<BleAdapterImpl>();

    // BLE 接收数据 → 转发给 Vehicle
    ble_adapter->set_data_callback([](const std::vector<uint8_t> &data) {
        if (vehicle) vehicle->on_rx_data(data);
    });

    // BLE 连接状态变化
    ble_adapter->set_status_callback([](bool connected) {
        if (vehicle) {
            vehicle->set_connected(connected);
            if (!connected) vehicle->clear_commands();  // prevent queue flood on reconnect
        }
        current_data.ble_connected = connected;
        if (!connected) {
            current_data.valid = false;
            last_poll_time = xTaskGetTickCount();  // reset poll timer
        }
        ESP_LOGI(TAG, "BLE %s", connected ? "CONNECTED" : "DISCONNECTED");
    });

    // 创建 Vehicle 实例
    vehicle = std::make_shared<TeslaBLE::Vehicle>(ble_adapter, storage_adapter);
    vehicle->set_vin(TESLA_VIN);

    // 注册 Tesla 协议回调
    vehicle->set_drive_state_callback(on_drive_state);
    vehicle->set_vehicle_status_callback(on_vehicle_status);

    // 检查配对状态（NVS 中有私钥即表示已配对）
    std::vector<uint8_t> key_data;
    paired = storage_adapter->load("private_key", key_data) && !key_data.empty();
    ESP_LOGI(TAG, "Paired=%s, VIN=%s", paired ? "YES" : "NO", TESLA_VIN);
}

// ---------- 等待 BLE 连接 ----------
static bool wait_for_ble_connection(int timeout_ms) {
    ESP_LOGI(TAG, "Waiting for BLE connection (timeout=%ds)...", timeout_ms / 1000);
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        vehicle->loop();
        ble_adapter->process();

        if (pending_data_ready) {
            pending_data_ready = false;
            pending_data.ble_connected  = current_data.ble_connected;
            pending_data.vehicle_awake  = current_data.vehicle_awake;
            current_data = pending_data;
        }

        if (ble_adapter->is_connected()) {
            // 给 Tesla 协议几秒钟完成握手
            TickType_t conn_time = xTaskGetTickCount();
            while ((xTaskGetTickCount() - conn_time) < pdMS_TO_TICKS(5000)) {
                vehicle->loop();
                ble_adapter->process();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            ESP_LOGI(TAG, "BLE connected + handshake complete");
            return true;
        }

        // 每 2 秒刷新一次 OLED 状态
        if (((xTaskGetTickCount() - start) % pdMS_TO_TICKS(2000)) < pdMS_TO_TICKS(50)) {
            display.show_text_lines("Scanning for", "Tesla vehicle...", "");
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

// ---------- 清除旧密钥并重新配对 ----------
static void force_repair() {
    ESP_LOGI(TAG, "Clearing old key and regenerating...");
    storage_adapter->remove("private_key");
    storage_adapter->remove("session_vcsec");
    storage_adapter->remove("session_infotainment");
    vehicle->regenerate_key();
    // Clear stale commands (e.g., VCSEC Poll from is_key_working)
    vehicle->clear_commands();
    // Disconnect to force a fresh BLE connection with the new key
    ble_adapter->disconnect();
    paired = false;
}

// ---------- 配对流程 ----------
static bool run_pairing() {
    ESP_LOGI(TAG, "Starting pairing flow...");

    // Wait for BLE reconnection first (may have been disconnected by force_repair)
    display.show_text_lines("Reconnecting...", "", "");
    TickType_t rstart = xTaskGetTickCount();
    while (!ble_adapter->is_connected() &&
           (xTaskGetTickCount() - rstart) < pdMS_TO_TICKS(30000)) {
        vehicle->loop();
        ble_adapter->process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!ble_adapter->is_connected()) {
        ESP_LOGW(TAG, "BLE not connected, cannot pair");
        return false;
    }

    display.show_pairing("Tap NFC key card");
    vehicle->pair(Keys_Role_ROLE_OWNER);

    // Wait for whitelist response or timeout (key card tap takes time)
    TickType_t start = xTaskGetTickCount();
    bool key_saved = false;

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(60000)) {
        vehicle->loop();
        ble_adapter->process();

        std::vector<uint8_t> check_key;
        if (!key_saved && storage_adapter->load("private_key", check_key) && !check_key.empty()) {
            key_saved = true;
        }

        // Key accepted by vehicle → whitelist response received + command completed
        if (current_data.valid) {
            ESP_LOGI(TAG, "Pairing SUCCESS - vehicle data received!");
            display.show_splash();
            vTaskDelay(pdMS_TO_TICKS(2000));
            return true;
        }

        // BLE disconnected while waiting → vehicle may have dropped us
        if (!ble_adapter->is_connected()) {
            ESP_LOGW(TAG, "BLE disconnected during pairing - may still have succeeded");
            return key_saved;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGW(TAG, "Pairing TIMEOUT (no data from vehicle)");
    return key_saved;  // Key was saved, may work after reconnection
}

// ---------- 检测密钥是否有效 ----------
static bool is_key_working(int timeout_ms) {
    ESP_LOGI(TAG, "Testing if stored key works (timeout=%ds)...", timeout_ms / 1000);

    // Trigger one VCSEC poll to start the auth flow
    vehicle->vcsec_poll();

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        try {
            vehicle->loop();
            ble_adapter->process();

            if (pending_data_ready) {
                pending_data_ready = false;
                pending_data.ble_connected  = current_data.ble_connected;
                pending_data.vehicle_awake  = current_data.vehicle_awake;
                current_data = pending_data;
            }

            // VCSEC session saved → key is valid
            std::vector<uint8_t> session_data;
            if (storage_adapter->load("session_vcsec", session_data) && !session_data.empty()) {
                ESP_LOGI(TAG, "VCSEC session established - key is valid!");
                return true;
            }

            if (current_data.valid) {
                ESP_LOGI(TAG, "Key is valid! Vehicle data received.");
                return true;
            }
        } catch (const std::exception &e) {
            ESP_LOGE(TAG, "Exception in key test: %s", e.what());
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGW(TAG, "Key validation timeout - no VCSEC session established");
    return false;
}

// ---------- 配对重置按键 ----------
static void init_pair_reset_button() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << PAIR_RESET_GPIO);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    ESP_LOGI(TAG, "Pair reset button on GPIO %d (hold %ds to clear)", PAIR_RESET_GPIO, PAIR_RESET_HOLD_MS / 1000);
}

// Returns true if button held for PAIR_RESET_HOLD_MS
static bool check_pair_reset_button() {
    static TickType_t press_start = 0;
    bool pressed = (gpio_get_level(PAIR_RESET_GPIO) == 0);  // active low
    TickType_t now = xTaskGetTickCount();

    if (pressed && press_start == 0) {
        press_start = now;
    } else if (!pressed) {
        press_start = 0;
        return false;
    }

    return press_start && (now - press_start) >= pdMS_TO_TICKS(PAIR_RESET_HOLD_MS);
}

// ---------- 主循环 ----------
extern "C" void app_main() {
    ESP_LOGI(TAG, "=== Tesla BLE Dashboard ===");
    ESP_LOGI(TAG, "VIN: %s", TESLA_VIN);

    init_display();
    init_tesla_ble();
    init_pair_reset_button();
    ble_adapter->init(TESLA_VIN);

    // 等待 BLE 连接（最多 30 秒）
    display.show_text_lines("Scanning for", "Tesla vehicle...", "");
    bool ble_ok = wait_for_ble_connection(30000);

    if (ble_ok) {
        if (paired) {
            // 已有密钥 → 先测试是否有效
            if (!is_key_working(15000)) {
                ESP_LOGW(TAG, "Stored key rejected by vehicle, re-pairing...");
                display.show_error("Key invalid");
                vTaskDelay(pdMS_TO_TICKS(2000));
                force_repair();
                paired = run_pairing();
            }
        } else {
            // 无密钥 → 直接配对
            paired = run_pairing();
        }
    } else {
        ESP_LOGW(TAG, "BLE connection failed, entering offline mode");
        display.show_error("No Tesla found");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    // Start with current time so first poll waits full interval
    last_poll_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "Entering main loop (paired=%d, connected=%d)",
             paired, ble_adapter->is_connected());

    while (true) {
        // Long-press button to clear pairing and force re-pair
        if (check_pair_reset_button()) {
            ESP_LOGW(TAG, "Pair reset button held! Clearing all keys...");
            display.show_text_lines("Resetting...", "Clear pairing data", "");
            force_repair();
            paired = run_pairing();
            last_poll_time = xTaskGetTickCount();
        }

        vehicle->loop();
        ble_adapter->process();

        if (pending_data_ready) {
            pending_data_ready = false;
            pending_data.ble_connected  = current_data.ble_connected;
            pending_data.vehicle_awake  = current_data.vehicle_awake;
            current_data = pending_data;
        }

        TickType_t now = xTaskGetTickCount();
        bool just_reconnected = (now - last_poll_time) < pdMS_TO_TICKS(5000);

        // Only poll if vehicle is known awake, OR if we haven't received any
        // status yet (first connection). Skip polling after reconnect until
        // vehicle wakes up, to avoid flooding the command queue.
        if (ble_adapter->is_connected() && !just_reconnected) {
            int interval = current_data.vehicle_awake ? POLL_INTERVAL_ACTIVE_MS
                                                       : POLL_INTERVAL_IDLE_MS;
            if ((now - last_poll_time) >= pdMS_TO_TICKS(interval)) {
                vehicle->drive_state_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP);
                last_poll_time = now;
            }
        }

        display.render_dashboard(current_data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
