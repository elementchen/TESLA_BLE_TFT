#!/bin/bash
cd "$(dirname "$0")"

# Python 3.12 from Homebrew required (macOS system Python 3.9 Tkinter is broken)
PYTHON=/opt/homebrew/bin/python3.12

if [ ! -f "$PYTHON" ]; then
    echo "Error: $PYTHON not found. Install with: brew install python@3.12"
    exit 1
fi

$PYTHON -c "import serial" 2>/dev/null || {
    echo "Installing pyserial..."
    $PYTHON -m pip install pyserial --break-system-packages -q
}

$PYTHON esp32_config_gui.py
