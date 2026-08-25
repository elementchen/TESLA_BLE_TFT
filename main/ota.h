#pragma once

/**
 * @brief Minimal OTA-update mode over USB-Serial-JTAG.
 *
 * Flow: host tool sends `OTA` (normal mode) → ota_request_reboot() sets an NVS
 * flag and reboots → on next boot ota_is_requested() returns true → app_main()
 * calls ota_run() BEFORE initializing BLE/display/LVGL.  This guarantees the
 * flash erase/write inside esp_ota_* has no radio (NimBLE) or SPI-display DMA
 * interference.  ota_run() blocks until the image is written and the device
 * reboots into the new firmware; on error it clears the flag and returns so
 * app_main() falls back to a normal boot of the current firmware.
 */

/** True if the ota_ready flag is set in NVS (we booted into OTA mode). */
bool ota_is_requested();

/** Set the ota_ready flag and reboot (called by the `OTA` config command). */
void ota_request_reboot();

/** Firmware version string from esp_app_desc (PROJECT_VER). */
const char *ota_firmware_version();

/** Blocking OTA-mode loop. Returns only on failure (normal boot fallback). */
void ota_run();
