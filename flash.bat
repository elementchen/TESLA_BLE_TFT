@echo off
set MSYSTEM=
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.4
set TOOLCHAIN=C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
set IDP_PYTHON=C:\Espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe
set PATH=%TOOLCHAIN%;%IDF_PATH%\tools;C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%
set PYTHON=%IDP_PYTHON%
cd /d e:\AI_coding_test\_ClaudeCode\Tesla_BLE_Dash
%PYTHON% %IDF_PATH%\tools\idf.py -p COM7 flash
