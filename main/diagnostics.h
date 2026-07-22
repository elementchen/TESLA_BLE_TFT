#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief Lightweight diagnostics collector for real-vehicle BLE debugging.
 *
 * Tracks polling success/failure rates, command queue depth extremes,
 * disconnect counts, and per-type response latencies (ring buffer of
 * last 32 samples). Call record_* methods from callbacks and the main
 * loop, then call print_summary() periodically to get a serial log dump.
 *
 * All methods are safe to call from any FreeRTOS task context.
 */
struct Diagnostics {
    // ─── Counters ────────────────────────────────────────────
    uint32_t polls_attempted   = 0;
    uint32_t polls_rejected    = 0;  // queue full
    uint32_t polls_succeeded   = 0;  // data callback fired
    uint32_t disconnects       = 0;
    uint32_t reconnects        = 0;
    uint32_t max_queue_depth   = 0;
    uint32_t msgs_rx           = 0;
    uint32_t msgs_tx           = 0;

    // ─── Per-type latency ring buffers ───────────────────────
    static constexpr uint8_t LAT_BUF_SIZE = 32;

    struct LatBuf {
        float   samples[LAT_BUF_SIZE] = {};
        uint8_t idx = 0;
        uint8_t count = 0;   // valid entries (≤ LAT_BUF_SIZE)

        void push(float ms) {
            samples[idx] = ms;
            idx = (idx + 1) % LAT_BUF_SIZE;
            if (count < LAT_BUF_SIZE) count++;
        }
        float avg() const {
            if (!count) return 0.0f;
            float sum = 0.0f;
            for (uint8_t i = 0; i < count; i++) sum += samples[i];
            return sum / count;
        }
    };

    LatBuf lat_drive;
    LatBuf lat_charge;
    LatBuf lat_climate;
    LatBuf lat_closures;
    LatBuf lat_tpms;

    // ─── API ─────────────────────────────────────────────────
    void record_poll_attempted()        { polls_attempted++; }
    void record_poll_rejected()         { polls_rejected++; }
    void record_poll_succeeded()        { polls_succeeded++; }
    void record_disconnect()            { disconnects++; }
    void record_reconnect()             { reconnects++; }
    void record_queue_depth(size_t d)   { if (d > max_queue_depth) max_queue_depth = d; }
    void record_rx()                    { msgs_rx++; }
    void record_tx()                    { msgs_tx++; }

    void record_latency(const char *type, uint32_t latency_ms);

    /** Log a summary line every N seconds. Returns true if printed. */
    bool print_summary_if_due(uint32_t now_ms, uint32_t interval_ms = 60000);

private:
    uint32_t last_print_ms_ = 0;
};

/** Global diagnostics instance (declared in diagnostics.cpp) */
extern Diagnostics g_diag;
