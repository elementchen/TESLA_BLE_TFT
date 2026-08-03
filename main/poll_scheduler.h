#pragma once

#include <cstdint>
#include <functional>
#include <vector>

/**
 * @brief Scene-driven polling scheduler for Tesla BLE telemetry.
 *
 * Key insight: the Tesla vehicle can process ~1.2 BLE commands per second.
 * To keep the command queue shallow and response latency low, we must limit
 * simultaneous active poll types.  Different driving scenes need different data:
 *
 *   CONNECTING  — only VCSEC (keep-alive), nothing else
 *   DRIVING     — closures (safety) + drive_state (speed/gear) alternating
 *   DOOR_OPEN   — closures ONLY, fastest poll rate to detect door close ASAP
 *   CHARGING    — charge_state ONLY, all others paused
 *
 * At most 2 poll types are active at once in DRIVING mode; 1 in others.
 * This keeps the command queue at 0-1 depth and response latency at ~800ms.
 */

enum class DashMode {
    CONNECTING,   // boot / BLE reconnecting — minimal polling
    DRIVING,      // gear != P, no doors open
    DOOR_OPEN,    // any door open — closures only
    CHARGING      // charging active — charge_state only
};

enum class PollPriority {
    HIGH = 0,
    MEDIUM = 1,
    LOW = 2,
    BACKGROUND = 3
};

struct PollSlot {
    const char *name;
    PollPriority priority;
    uint32_t interval_ms;         // base interval in DRIVING mode
    uint32_t door_interval_ms;    // interval in DOOR_OPEN mode (0 = disabled)
    uint32_t charge_interval_ms;  // interval in CHARGING mode (0 = disabled)
    uint32_t connect_interval_ms; // interval in CONNECTING mode (0 = disabled)
    uint32_t last_polled_at_ms;
    bool enabled = false;
    uint32_t effective_interval = 0;
    std::function<void()> poll_func;
};

class PollScheduler {
public:
    PollScheduler() = default;

    void register_poll(const char *name, PollPriority priority,
                       uint32_t drive_interval_ms,
                       uint32_t door_interval_ms,
                       uint32_t charge_interval_ms,
                       uint32_t connect_interval_ms,
                       std::function<void()> poll_func);

    /**
     * @brief Switch the dashboard scene.
     *
     * Disables irrelevant poll types and adjusts intervals for the current mode.
     */
    void set_mode(DashMode mode);

    /** Dynamically enable/disable a named slot without changing mode. */
    void set_slot_enabled(const char *name, bool enabled);

    /** Periodic BLE quiet window — pauses all polling to let the vehicle
     *  communicate with BLE TPMS sensors. */
    void set_quiet(bool quiet) { quiet_ = quiet; }
    bool is_quiet() const { return quiet_; }

    DashMode get_mode() const { return mode_; }

    /**
     * @brief Process one poll cycle. At most ONE poll dispatched per call.
     */
    void process(uint32_t now_ms, size_t queue_depth, size_t max_queue);

    size_t get_pending_count() const;

private:
    std::vector<PollSlot> slots_;
    DashMode mode_ = DashMode::CONNECTING;
    bool mode_initialized_ = false;
    bool quiet_ = false;  // BLE quiet window for TPMS
    uint32_t last_dispatch_ms_ = 0;

    // Cooldown adapts to active poll count: fewer types = faster dispatch
    uint32_t current_cooldown_ms() const;

    static constexpr size_t BACKPRESSURE_THRESHOLD = 3;
};
