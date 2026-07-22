#pragma once

#include <cstdint>
#include <functional>
#include <vector>

/**
 * @brief Priority-based polling scheduler for Tesla BLE telemetry.
 *
 * Replaces the all-at-once infotainment_poll() to prevent command queue
 * saturation. Each poll type has its own interval timer. At most one poll
 * is dispatched per process() call, and only when the command queue has
 * room (backpressure).
 *
 * Polling intervals adapt to vehicle state:
 *   - Parked (gear == P):  default intervals
 *   - Moving (gear != P):  HIGH frequency doubled, LOW/BACKGROUND halved
 */
enum class PollPriority {
    HIGH = 0,       // ~500ms → 250ms moving: speed/gear (drive_state)
    MEDIUM = 1,      // ~1s:  battery, charge state, VCSEC status
    LOW = 2,          // ~3s → 6s moving: climate, closures
    BACKGROUND = 3     // ~10s → 20s moving: tire pressure
};

struct PollSlot {
    const char *name;
    PollPriority priority;
    uint32_t interval_ms;        // base interval (parked)
    uint32_t moving_interval_ms;  // interval when vehicle is moving
    uint32_t last_polled_at_ms;   // FreeRTOS tick * portTICK_PERIOD_MS
    std::function<void()> poll_func;

    /** Effective interval for current vehicle state */
    uint32_t effective_interval(bool moving) const {
        return moving ? moving_interval_ms : interval_ms;
    }
};

class PollScheduler {
public:
    PollScheduler() = default;

    /**
     * @brief Register a poll type with its base interval.
     *
     * @param name           Debug label for logging
     * @param priority       HIGH / MEDIUM / LOW / BACKGROUND
     * @param interval_ms    Base polling interval when parked
     * @param moving_factor  When moving, interval is multiplied by this
     *                       (e.g. 0.5 = twice as fast, 2.0 = half as fast)
     * @param poll_func      Lambda that calls vehicle->xxx_state_poll()
     */
    void register_poll(const char *name, PollPriority priority,
                       uint32_t interval_ms, float moving_factor,
                       std::function<void()> poll_func);

    /**
     * @brief Notify scheduler whether the vehicle is moving.
     *
     * Called from main loop, typically: moving = (gear != 'P')
     */
    void set_vehicle_moving(bool moving) { vehicle_moving_ = moving; }

    /**
     * @brief Process one poll cycle.
     *
     * Dispatches at most ONE poll whose interval has elapsed, and only
     * if the command queue has room (queue_depth < max - margin).
     *
     * @param now_ms     Current timestamp (e.g. xTaskGetTickCount * portTICK_PERIOD_MS)
     * @param queue_depth Current command queue size
     * @param max_queue  MAX_COMMAND_QUEUE_SIZE
     */
    void process(uint32_t now_ms, size_t queue_depth, size_t max_queue);

    /** How many slots currently have elapsed timers */
    size_t get_pending_count() const;

private:
    std::vector<PollSlot> slots_;
    bool vehicle_moving_ = false;
    uint32_t last_dispatch_ms_ = 0;

    // ── Tuning constants ──────────────────────────────────────────
    // Don't dispatch faster than 500ms (vehicle processes ~1 cmd/sec)
    static constexpr uint32_t MIN_DISPATCH_INTERVAL_MS = 500;
    // Stop dispatching when queue has 6+ commands pending
    // (equilibrium: 6 / 1.25cmd_sec_drain ≈ 5 seconds to clear)
    static constexpr size_t BACKPRESSURE_THRESHOLD = 6;
};
