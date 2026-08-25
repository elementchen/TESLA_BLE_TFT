#include "config_manager.h"
#include "ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>
#include <cstdio>

static constexpr const char *TAG = "ConfigMgr";
static constexpr const char *NVS_NS  = "config";

// ── In-memory config cache ──────────────────────────────────
static std::string g_vin;
static std::string g_display_model = "ILI9341";
static DisplayPins g_pins;
static bool g_dirty = false;

// ── Serial command buffer ───────────────────────────────────
static std::string g_cmd_buf;

// ── Helpers ─────────────────────────────────────────────────
static void load_from_nvs() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS config namespace not found, using defaults");
        return;
    }

    // VIN
    size_t len = 0;
    if (nvs_get_str(h, "vin", nullptr, &len) == ESP_OK) {
        char *buf = new char[len];
        if (nvs_get_str(h, "vin", buf, &len) == ESP_OK) {
            g_vin = buf;
            ESP_LOGI(TAG, "Loaded VIN=%s", g_vin.c_str());
        }
        delete[] buf;
    }

    // Display model
    len = 0;
    if (nvs_get_str(h, "model", nullptr, &len) == ESP_OK) {
        char *buf = new char[len];
        if (nvs_get_str(h, "model", buf, &len) == ESP_OK) {
            g_display_model = buf;
            ESP_LOGI(TAG, "Loaded display=%s", g_display_model.c_str());
        }
        delete[] buf;
    }

    // Display pins (binary blob)
    len = sizeof(DisplayPins);
    DisplayPins stored;
    if (nvs_get_blob(h, "pins", &stored, &len) == ESP_OK && len == sizeof(DisplayPins)) {
        g_pins = stored;
        ESP_LOGI(TAG, "Loaded SPI pins: SCK=%d MOSI=%d MISO=%d DC=%d CS=%d RST=%d BLK=%d",
                 g_pins.sck, g_pins.mosi, g_pins.miso, g_pins.dc, g_pins.cs, g_pins.rst, g_pins.blk);
    }

    nvs_close(h);
}

static void save_to_nvs() {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %d", err);
        return;
    }

    nvs_set_str(h, "vin", g_vin.c_str());
    nvs_set_str(h, "model", g_display_model.c_str());
    nvs_set_blob(h, "pins", &g_pins, sizeof(DisplayPins));
    nvs_commit(h);
    nvs_close(h);
    g_dirty = false;
    ESP_LOGI(TAG, "Config saved to NVS");
}

static void print_status() {
    printf("\n=== ESP32 Config ===\n");
    printf("VIN:          %s\n", g_vin.empty() ? "(default from Kconfig)" : g_vin.c_str());
    printf("Display:      %s\n", g_display_model.c_str());
    printf("SPI Pins:     SCK=%d MOSI=%d MISO=%d DC=%d CS=%d RST=%d BLK=%d\n",
           g_pins.sck, g_pins.mosi, g_pins.miso, g_pins.dc, g_pins.cs, g_pins.rst, g_pins.blk);
    printf("Unsaved:      %s\n", g_dirty ? "YES" : "no");
    printf("====================\n\n");
}

// ── Public API ──────────────────────────────────────────────

void config_manager_init() {
    // Load NVS config (or use compile-time defaults)
    load_from_nvs();

    // VIN fallback chain: NVS → Kconfig
    if (g_vin.empty()) {
#ifdef CONFIG_TESLA_DASH_VIN
        g_vin = CONFIG_TESLA_DASH_VIN;
#endif
        if (g_vin.empty() || g_vin == "YOUR_TESLA_VIN_HERE") {
            ESP_LOGW(TAG, "No VIN configured! Use USB config tool: SET VIN=<17 chars>");
            g_vin = "";
        } else {
            ESP_LOGI(TAG, "Using Kconfig VIN=%s", g_vin.c_str());
        }
    }

    ESP_LOGI(TAG, "Config ready.  Send 'HELP' over serial for commands.");
}

void config_manager_process_command(const std::string &line) {
    if (line.empty()) return;

    // Echo the command back
    printf("> %s\n", line.c_str());

    if (line == "HELP") {
        printf("Commands:\n");
        printf("  STATUS              Show current config\n");
        printf("  SET VIN=<17 chars>  Set vehicle VIN\n");
        printf("  SET MODEL=<name>    Set display model (e.g. ILI9341)\n");
        printf("  SET PINS=s,m,o,d,c,r,b   Set SPI pins (SCK,MOSI,MISO,DC,CS,RST,BLK)\n");
        printf("  SAVE                Write config to NVS\n");
        printf("  LOAD                Reload config from NVS\n");
        printf("  VERSION             Show firmware version\n");
        printf("  OTA                 Reboot into OTA update mode\n");
        printf("  REBOOT              Restart ESP32\n");
    }
    else if (line == "STATUS") {
        print_status();
    }
    else if (line.rfind("SET VIN=", 0) == 0) {
        std::string vin = line.substr(8);
        if (vin.length() == 17) {
            g_vin = vin;
            g_dirty = true;
            printf("OK: VIN=%s (need SAVE)\n", g_vin.c_str());
        } else {
            printf("ERR: VIN must be exactly 17 characters\n");
        }
    }
    else if (line.rfind("SET MODEL=", 0) == 0) {
        g_display_model = line.substr(10);
        g_dirty = true;
        printf("OK: MODEL=%s (need SAVE)\n", g_display_model.c_str());
    }
    else if (line.rfind("SET PINS=", 0) == 0) {
        DisplayPins p;
        int n = sscanf(line.c_str(), "SET PINS=%hhd,%hhd,%hhd,%hhd,%hhd,%hhd,%hhd",
                       &p.sck, &p.mosi, &p.miso, &p.dc, &p.cs, &p.rst, &p.blk);
        if (n == 7) {
            g_pins = p;
            g_dirty = true;
            printf("OK: PINS set (need SAVE)\n");
        } else {
            printf("ERR: need 7 values: SCK,MOSI,MISO,DC,CS,RST,BLK (got %d)\n", n);
        }
    }
    else if (line == "SAVE") {
        save_to_nvs();
        printf("OK: Saved to NVS\n");
    }
    else if (line == "LOAD") {
        load_from_nvs();
        print_status();
    }
    else if (line == "REBOOT") {
        printf("Rebooting...\n");
        esp_restart();
    }
    else if (line == "VERSION") {
        printf("Version: %s\n", ota_firmware_version());
    }
    else if (line == "OTA") {
        ota_request_reboot();
    }
    else {
        printf("ERR: Unknown command. Type HELP\n");
    }
}

std::string config_get_vin() {
    return g_vin;
}

std::string config_get_display_model() {
    return g_display_model;
}

DisplayPins config_get_display_pins() {
    return g_pins;
}
