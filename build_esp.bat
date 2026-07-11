@echo off
set MSYSTEM=
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.5.4
set TOOLCHAIN=C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
set IDP_PYTHON=C:\Espressif\python_env\idf5.5_py3.10_env\Scripts\python.exe
set CMAKE=C:\Espressif\tools\cmake\3.30.2\bin\cmake.exe
set NINJA=C:\Espressif\tools\ninja\1.12.1\ninja.exe
set PATH=%TOOLCHAIN%;%IDF_PATH%\tools;C:\Espressif\tools\idf-git\2.44.0\cmd;%CMAKE%\..;%NINJA%\..;%PATH%
set PYTHON=%IDP_PYTHON%
cd /d "%~dp0"
%PYTHON% %IDF_PATH%\tools\idf.py set-target esp32s3
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
%PYTHON% %IDF_PATH%\tools\idf.py build
