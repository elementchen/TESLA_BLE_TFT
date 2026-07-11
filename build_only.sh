#!/bin/bash
# macOS build script for Tesla BLE Dashboard
export IDF_PATH="$HOME/esp/esp-idf-v5.5.4"
source "$IDF_PATH/export.sh" 2>&1
idf.py build
