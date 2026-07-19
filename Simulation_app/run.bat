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

:: 3. 启动 BLE 蓝牙车机外设广播服务
echo [+] 正在启动 BLE 蓝牙车机外设广播服务...
echo [+] 请确保您的电脑蓝牙已在系统设置中开启。
python sim_server.py

pause
