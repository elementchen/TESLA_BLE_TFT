#include "poll_scheduler.h"
#include "diagnostics.h"
#include "esp_log.h"
#include <cstring>

static constexpr const char *TAG = "PollSched";

void PollScheduler::register_poll(const char *name, PollPriority priority,
                                   uint32_t drive_interval_ms,
                                   uint32_t door_interval_ms,
                                   uint32_t charge_interval_ms,
                                   uint32_t connect_interval_ms,
                                   std::function<void()> poll_func) {
    slots_.push_back({
        .name = name,
        .priority = priority,
        .interval_ms = drive_interval_ms,
        .door_interval_ms = door_interval_ms,
        .charge_interval_ms = charge_interval_ms,
        .connect_interval_ms = connect_interval_ms,
        .last_polled_at_ms = 0,
        .enabled = false,
        .effective_interval = 0,
        .poll_func = std::move(poll_func),
    });
}

void PollScheduler::set_mode(DashMode mode) {
    if (mode == mode_ && mode_initialized_) return;
    const char *names[] = {"CONNECTING", "DRIVING", "DOOR_OPEN", "CHARGING"};
    ESP_LOGI(TAG, "Mode: %s -> %s", names[static_cast<int>(mode_)], names[static_cast<int>(mode)]);
    mode_ = mode;
    mode_initialized_ = true;

    for (auto &slot : slots_) {
        uint32_t iv = 0;
        switch (mode) {
            case DashMode::CONNECTING: iv = slot.connect_interval_ms; break;
            case DashMode::DRIVING:    iv = slot.interval_ms;         break;
            case DashMode::DOOR_OPEN:  iv = slot.door_interval_ms;    break;
            case DashMode::CHARGING:   iv = slot.charge_interval_ms;  break;
        }
        slot.enabled = (iv > 0);
        slot.effective_interval = iv;
        if (!slot.enabled) {
            ESP_LOGD(TAG, "  %s: DISABLED", slot.name);
        }
    }
}

void PollScheduler::set_slot_enabled(const char *name, bool enabled) {
    for (auto &slot : slots_) {
        if (std::strcmp(slot.name, name) == 0) {
            if (slot.enabled != enabled) {
                ESP_LOGI(TAG, "Slot '%s': %s", name, enabled ? "ENABLED" : "DISABLED (highway)");
                slot.enabled = enabled;
            }
            return;
        }
    }
}

uint32_t PollScheduler::current_cooldown_ms() const {
    // Count *near-term competing* slots to determine dispatch rate.
    // Long-interval background slots (climate 900s / tpms 300s / charge@P 120s)
    // almost never fire and must not throttle the fast drive/closures slots.
    int active = 0;
    for (auto &s : slots_) {
        if (s.enabled && s.effective_interval > 0 && s.effective_interval <= 10000) active++;
    }
    if (active <= 1) return 400;   // single-type mode: dispatch aggressively
    if (active <= 2) return 600;   // dual-type mode: balanced
    return 1000;                    // multi-type: slow down
}

size_t PollScheduler::get_pending_count() const { return 0; }

void PollScheduler::process(uint32_t now_ms, size_t queue_depth, size_t max_queue) {
    // ── BLE quiet window (TPMS) ─────────────────────────────────
    if (quiet_) return;

    // ── Cooldown ────────────────────────────────────────────────
    uint32_t cooldown = current_cooldown_ms();
    if (last_dispatch_ms_ != 0 && now_ms - last_dispatch_ms_ < cooldown) {
        return;
    }

    // ── Backpressure ────────────────────────────────────────────
    if (queue_depth >= BACKPRESSURE_THRESHOLD) {
        g_diag.record_poll_rejected();
        return;
    }

    // ── Pick the most-overdue ENABLED poll ──────────────────────
    PollSlot *best = nullptr;
    float best_score = 0.0f;

    for (auto &slot : slots_) {
        if (!slot.enabled) continue;
        uint32_t interval = slot.effective_interval;
        if (slot.last_polled_at_ms == 0) {
            // Never polled: dispatch immediately
            best = &slot;
            break;
        }
        uint32_t elapsed = now_ms - slot.last_polled_at_ms;
        if (elapsed >= interval) {
            float overdue = static_cast<float>(elapsed) / static_cast<float>(interval);
            // Subtle priority bonus for tie-breaking only
            float pri_bonus = 0.0f;
            switch (slot.priority) {
                case PollPriority::HIGH:       pri_bonus = 0.2f; break;
                case PollPriority::MEDIUM:     pri_bonus = 0.1f; break;
                case PollPriority::LOW:        pri_bonus = 0.05f; break;
                case PollPriority::BACKGROUND: pri_bonus = 0.0f; break;
            }
            float score = overdue + pri_bonus;
            if (score > best_score) {
                best_score = score;
                best = &slot;
            }
        }
    }

    if (best) {
        uint32_t elapsed = best->last_polled_at_ms ? (now_ms - best->last_polled_at_ms) : 0;
        ESP_LOGD(TAG, "%s (elapsed=%" PRIu32 "ms, queue=%zu/%zu, cooldown=%" PRIu32 "ms)",
                 best->name, elapsed, queue_depth, max_queue, cooldown);
        best->last_polled_at_ms = now_ms;
        last_dispatch_ms_ = now_ms;
        g_diag.record_poll_attempted();
        best->poll_func();
    }
}
