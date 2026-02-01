@echo off
REM Legend2 地图查看器启动脚本

echo Starting Legend2 Map Viewer...
echo.

REM 检查 Python 是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python is not installed or not in PATH
    echo Please install Python 3.8 or later
    pause
    exit /b 1
)

REM 检查依赖
echo Checking dependencies...
python -c "import PyQt5" >nul 2>&1
if errorlevel 1 (
    echo Installing dependencies...
    pip install -r requirements.txt
    if errorlevel 1 (
        echo Error: Failed to install dependencies
        pause
        exit /b 1
    )
)

REM 启动程序
echo.
echo Launching Map Viewer...
python main.py %*

if errorlevel 1 (
    echo.
    echo Program exited with error
    pause
)
