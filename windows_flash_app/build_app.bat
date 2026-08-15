@echo off
REM Build standalone Windows exe files for ESP32 Config Tool
REM Requires: Python 3.10+ with pyserial + pyinstaller
REM Output: dist\ESP32_Config_Tool.exe (GUI) and dist\esp32_config.exe (CLI)

cd /d "%~dp0"

python -m pip install pyserial pyinstaller -q
if errorlevel 1 (
    echo Error: failed to install dependencies
    exit /b 1
)

rmdir /s /q build dist 2>nul

echo Building GUI exe...
python -m PyInstaller --noconfirm --clean --onefile --windowed --name ESP32_Config_Tool esp32_config_gui.py
if errorlevel 1 (
    echo Error: GUI build failed
    exit /b 1
)

echo Building CLI exe...
python -m PyInstaller --noconfirm --clean --onefile --console --name esp32_config esp32_config.py
if errorlevel 1 (
    echo Error: CLI build failed
    exit /b 1
)

echo.
echo Done. Output files:
dir /b dist\*.exe
echo.
echo To distribute: zip the exe files in dist\ and upload to GitHub Release.
