#include "poll_scheduler.h"
#include "diagnostics.h"
#include "esp_log.h"

static constexpr const char *TAG = "PollSched";

void PollScheduler::register_poll(const char *name, PollPriority priority,
                                   uint32_t interval_ms, float moving_factor,
                                   std::function<void()> poll_func) {
    uint32_t moving_ms = static_cast<uint32_t>(interval_ms * moving_factor);
    if (moving_ms < 100) moving_ms = 100; // floor: 100ms

    slots_.push_back({
        .name = name,
        .priority = priority,
        .interval_ms = interval_ms,
        .moving_interval_ms = moving_ms,
        .last_polled_at_ms = 0,
        .poll_func = std::move(poll_func),
    });
}

size_t PollScheduler::get_pending_count() const {
    return 0; // informational
}

void PollScheduler::process(uint32_t now_ms, size_t queue_depth, size_t max_queue) {
    // ── Cooldown: don't dispatch faster than the vehicle can respond ──
    // Real Tesla BLE processes ~1 command/second (each taking 800-1000ms).
    // Dispatches faster than 500ms just pile up in the queue.
    if (last_dispatch_ms_ != 0 && now_ms - last_dispatch_ms_ < MIN_DISPATCH_INTERVAL_MS) {
        return;
    }

    // ── Strict backpressure: stop dispatching when > 6 commands queued ──
    // With ~1 cmd/sec drain rate, a queue of 6 takes ~5 seconds to clear.
    // Higher than 8 means minutes of stale data for low-priority types.
    if (queue_depth >= BACKPRESSURE_THRESHOLD) {
        g_diag.record_poll_rejected();
        return;
    }

    // ── Select most-overdue poll (priority only as tiebreaker) ──
    // Overdue factor = elapsed_ms / interval_ms.  A poll due 3x its interval
    // (e.g. closures 9s overdue at 3s interval) beats drive_state 1.3x overdue.
    PollSlot *best = nullptr;
    float best_score = 0.0f;
    uint32_t best_interval = 0;

    for (auto &slot : slots_) {
        uint32_t interval = slot.effective_interval(vehicle_moving_);
        if (slot.last_polled_at_ms == 0) {
            // Never polled yet — always dispatch once to get initial data
            best = &slot;
            break;
        }
        uint32_t elapsed = now_ms - slot.last_polled_at_ms;
        if (elapsed >= interval) {
            float overdue = static_cast<float>(elapsed) / static_cast<float>(interval);
            // Priority bonus: subtle nudge so HIGH wins ties, but overdue factor dominates.
            // Starvation guard: any slot > 15s overdue gets a huge bonus to force dispatch.
            float pri_bonus = 0.0f;
            if (elapsed > 15000) {
                pri_bonus = 100.0f;  // starvation guard: force dispatch
            } else {
                switch (slot.priority) {
                    case PollPriority::HIGH:       pri_bonus = 0.3f; break;
                    case PollPriority::MEDIUM:     pri_bonus = 0.15f; break;
                    case PollPriority::LOW:        pri_bonus = 0.05f; break;
                    case PollPriority::BACKGROUND: pri_bonus = 0.0f; break;
                }
            }
            float score = overdue + pri_bonus;
            if (score > best_score) {
                best_score = score;
                best = &slot;
                best_interval = interval;
            }
        }
    }

    if (best) {
        uint32_t elapsed = now_ms - best->last_polled_at_ms;
        ESP_LOGD(TAG, "Dispatching %s (pri=%d, overdue=%.1fx, queue=%zu/%zu)",
                 best->name, static_cast<int>(best->priority),
                 best_interval ? static_cast<float>(elapsed) / best_interval : 0.0f,
                 queue_depth, max_queue);
        best->last_polled_at_ms = now_ms;
        last_dispatch_ms_ = now_ms;
        g_diag.record_poll_attempted();
        best->poll_func();
    }
}
