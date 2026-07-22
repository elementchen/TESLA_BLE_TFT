#include "diagnostics.h"
#include "esp_log.h"
#include <cstring>

static constexpr const char *TAG = "Diag";

Diagnostics g_diag;

void Diagnostics::record_latency(const char *type, uint32_t latency_ms) {
    float ms = static_cast<float>(latency_ms);
    if      (std::strcmp(type, "drive")    == 0) lat_drive.push(ms);
    else if (std::strcmp(type, "charge")   == 0) lat_charge.push(ms);
    else if (std::strcmp(type, "climate")  == 0) lat_climate.push(ms);
    else if (std::strcmp(type, "closures") == 0) lat_closures.push(ms);
    else if (std::strcmp(type, "tpms")     == 0) lat_tpms.push(ms);
}

bool Diagnostics::print_summary_if_due(uint32_t now_ms, uint32_t interval_ms) {
    if (last_print_ms_ != 0 && now_ms - last_print_ms_ < interval_ms) {
        return false;
    }
    last_print_ms_ = now_ms;

    ESP_LOGI(TAG, "=== %" PRIu32 "s summary ===", interval_ms / 1000);
    ESP_LOGI(TAG, "Polls: attempted=%" PRIu32 " ok=%" PRIu32 " rejected=%" PRIu32 " queue_max=%" PRIu32,
             polls_attempted, polls_succeeded, polls_rejected, max_queue_depth);
    ESP_LOGI(TAG, "BLE: rx=%" PRIu32 " tx=%" PRIu32 " disc=%" PRIu32 " reconn=%" PRIu32,
             msgs_rx, msgs_tx, disconnects, reconnects);
    ESP_LOGI(TAG, "Latency(ms): drive=%.0f charge=%.0f climate=%.0f closures=%.0f tpms=%.0f",
             lat_drive.avg(), lat_charge.avg(), lat_climate.avg(),
             lat_closures.avg(), lat_tpms.avg());

    // Reset counters for next interval
    polls_attempted  = 0;
    polls_rejected   = 0;
    polls_succeeded  = 0;
    max_queue_depth  = 0;
    msgs_rx          = 0;
    msgs_tx          = 0;
    // disconnect/reconnect counters persist across intervals

    return true;
}
