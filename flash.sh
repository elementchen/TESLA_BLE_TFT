#!/bin/bash
# macOS flash script for Tesla BLE Dashboard
# Usage: ./flash.sh [port]
# Default port: /dev/cu.usbmodem*
PORT="${1:-/dev/cu.usbmodem*}"
export IDF_PATH="$HOME/esp/esp-idf-v5.5.4"
source "$IDF_PATH/export.sh" 2>&1
idf.py -p "$PORT" flash
