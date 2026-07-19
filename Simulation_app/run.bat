@echo off
title Tesla Dash HMI Simulator Launcher
color 0B

echo ==========================================================
echo        Tesla BLE HMI Windows Simulator Launcher
echo ==========================================================
echo.

:: 1. 检查 Python 环境
python --version >nul 2>&1
if %errorlevel% neq 0 (
    color 0C
    echo [-] 错误: 系统未检测到 Python 3 环境，请先安装 Python 3 并将其加入系统 PATH。
    pause
    exit /b
)

:: 2. 自动安装/核准依赖项
echo [+] 正在验证并核准依赖项...
pip install -r requirements.txt
if %errorlevel% neq 0 (
    echo [-] 依赖项安装失败，请检查网络连接。
    pause
    exit /b
)
echo.

:: 3. 让用户选择仿真模式
echo ==========================================================
echo 请选择您的车机遥测仿真注入模式:
echo.
echo   [1] Low Energy 蓝牙外设广播模拟 (BLE Mode) - 无线连接
echo   [2] 串口直连极速注入 (UART Mode) - 一插即用 100% 稳定
echo ==========================================================
echo.
set /p opt="请选择模式 (默认 2): "

if "%opt%"=="1" (
    echo.
    echo [+] 正在启动 BLE 蓝牙车机外设广播服务...
    python sim_server.py --mode ble
) else (
    echo.
    set /p port="请输入 ESP32 串口端口号 (默认 COM10): "
    if "%port%"=="" set port=COM10
    echo.
    echo [+] 正在启动串口极速注入服务 (端口: %port%)...
    python sim_server.py --mode uart --port %port%
)

pause
