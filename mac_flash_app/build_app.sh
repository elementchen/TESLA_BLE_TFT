#!/bin/bash
# Build standalone macOS .app bundle for ESP32 Config Tool
# Requires: Homebrew Python 3.12 + py2app
# Output: dist/ESP32 Config Tool.app (double-click to run, no dependencies needed)

cd "$(dirname "$0")"

PYTHON=/opt/homebrew/bin/python3.12

if [ ! -f "$PYTHON" ]; then
    echo "Error: $PYTHON not found. Install: brew install python@3.12"
    exit 1
fi

$PYTHON -m pip install pyserial py2app --break-system-packages -q

rm -rf build dist

echo "Building..."
$PYTHON setup.py py2app 2>&1 | grep -E "Done|error|Error" || true

if [ -d "dist/ESP32 Config Tool.app" ]; then
    echo ""
    echo "✅  dist/ESP32 Config Tool.app"
    echo "    $(du -sh dist/ESP32\ Config\ Tool.app | cut -f1)"
    echo ""
    echo "To distribute: zip the .app and share."
else
    echo "❌ Build failed"
    exit 1
fi
