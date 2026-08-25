#include "ota.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "OTA";
static constexpr const char *NVS_NS = "ota";
static constexpr const char *NVS_KEY = "ready";

// ── ota_ready flag ──────────────────────────────────────────
static void set_flag(uint8_t v) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_KEY, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool ota_is_requested() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t v = 0;
    nvs_get_u8(h, NVS_KEY, &v);
    nvs_close(h);
    return v == 1;
}

void ota_request_reboot() {
    set_flag(1);
    printf("OK: rebooting into OTA mode\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

const char *ota_firmware_version() {
    return esp_app_get_description()->version;
}

void ota_run() {
    auto announce = []() {
        printf("OTA:READY %s\n", ota_firmware_version());
        fflush(stdout);
    };
    announce();

    // 1. Wait for "OTA START=<size>".
    //    Re-announce every 1s so a (re)connecting host always sees a fresh
    //    banner — the one printed at boot may be lost across USB re-enumeration.
    char line[128];
    size_t len = 0;
    uint32_t size = 0;
    TickType_t last_banner = xTaskGetTickCount();
    while (true) {
        if (xTaskGetTickCount() - last_banner >= pdMS_TO_TICKS(1000)) {
            announce();
            last_banner = xTaskGetTickCount();
        }

        char ch = 0;
        int n = usb_serial_jtag_read_bytes(reinterpret_cast<uint8_t *>(&ch), 1, 0);
        if (n == 1) {
            if (ch == '\n' || ch == '\r') {
                line[len] = '\0';
                if (len > 0) {
                    unsigned int parsed = 0;
                    if (sscanf(line, "OTA START=%u", &parsed) == 1 && parsed > 0) {
                        size = parsed;
                        break;
                    }
                    ESP_LOGW(TAG, "Ignoring non-OTA line: '%s'", line);
                    len = 0;
                }
            } else if (ch >= 0x20 && ch <= 0x7E && len < sizeof(line) - 1) {
                line[len++] = ch;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));   // idle yield
        }
    }

    // 2. Resolve the inactive OTA partition and begin
    const esp_partition_t *update = esp_ota_get_next_update_partition(nullptr);
    if (!update) {
        ESP_LOGE(TAG, "No OTA partition found");
        set_flag(0);
        printf("OTA:ERR no_update_partition\n");
        fflush(stdout);
        return;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        set_flag(0);
        printf("OTA:ERR begin_failed: %s\n", esp_err_to_name(err));
        fflush(stdout);
        return;
    }
    printf("OTA:OK\n");
    fflush(stdout);

    // 3. Raw binary streaming into the OTA slot; ACK at each 1KB boundary
    uint32_t received = 0;
    uint8_t buf[1024];
    bool failed = false;
    while (received < size) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n <= 0) continue;
        if (esp_ota_write(handle, buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed");
            failed = true;
            break;
        }
        uint32_t prev = received;
        received += static_cast<uint32_t>(n);
        if ((received / 1024) > (prev / 1024) || received >= size) {
            printf("OTA:ACK %" PRIu32 "\n", received);
            fflush(stdout);
        }
    }

    if (failed) {
        esp_ota_abort(handle);
        set_flag(0);
        printf("OTA:ERR write_failed\n");
        fflush(stdout);
        return;
    }

    // 4. Finalize and switch boot partition
    if (esp_ota_end(handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed");
        set_flag(0);
        printf("OTA:ERR end_failed\n");
        fflush(stdout);
        return;
    }
    if (esp_ota_set_boot_partition(update) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        set_flag(0);
        printf("OTA:ERR set_boot_failed\n");
        fflush(stdout);
        return;
    }

    set_flag(0);
    printf("OTA:DONE\n");
    fflush(stdout);

    ESP_LOGI(TAG, "OTA success — rebooting into new firmware");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
