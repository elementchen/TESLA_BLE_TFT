#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Runtime configuration via NVS + USB serial.
 *
 * Priority: NVS stored value → Kconfig default → hardcoded fallback.
 * Desktop tool writes to NVS via simple text commands over USB serial.
 */

// ── Display pin configuration ──────────────────────────────
struct DisplayPins {
    int8_t sck  = 12;
    int8_t mosi = 11;
    int8_t miso = 13;
    int8_t dc   = 46;
    int8_t cs   = 10;
    int8_t rst  = -1;   // -1 = not connected
    int8_t blk  = 45;
};

// ── API ─────────────────────────────────────────────────────

/** Initialize NVS config namespace. Call once at boot. */
void config_manager_init();

/** Process one line of serial command. Call from main loop when Serial data available. */
void config_manager_process_command(const std::string &line);

/** Get VIN (NVS → Kconfig fallback). */
std::string config_get_vin();

/** Get display model string (e.g. "ILI9341"). */
std::string config_get_display_model();

/** Get display SPI pins. */
DisplayPins config_get_display_pins();
