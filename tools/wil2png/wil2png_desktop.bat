@echo off
setlocal

rem Locate project root and wil2png executable (prefer Release, fallback to Debug)
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\\..") do set "ROOT=%%~fI"
set "EXE=%ROOT%\\build\\bin\\Release\\wil2png.exe"
if not exist "%EXE%" set "EXE=%ROOT%\\build\\bin\\Debug\\wil2png.exe"
if not exist "%EXE%" (
    echo [wil2png] Executable not found. Build the project first.
    exit /b 1
)

rem Resolve input (drag-drop or file dialog)
set "INPUT=%~1"
if "%INPUT%"=="" (
    powershell -NoLogo -NoProfile -Command ^
        "Add-Type -AssemblyName System.Windows.Forms; $dlg = New-Object Windows.Forms.OpenFileDialog; $dlg.Filter = 'WIL files (*.wil)|*.wil'; $dlg.Title = 'Select a WIL file'; if ($dlg.ShowDialog() -eq 'OK') { Write-Output $dlg.FileName }" ^
        > "%temp%\\wil2png_input.txt"
    set /p INPUT=<"%temp%\\wil2png_input.txt"
)

if "%INPUT%"=="" (
    echo [wil2png] No input selected.
    exit /b 1
)

rem Detect directory vs file for batch mode
set "MODE=file"
if exist "%INPUT%\\NUL" set "MODE=batch"

rem Resolve output directory
set "OUTPUT=%~2"
if "%OUTPUT%"=="" (
    if "%MODE%"=="batch" (
        set "OUTPUT=%INPUT%_png"
    ) else (
        for %%F in ("%INPUT%") do set "OUTPUT=%%~dpnF_png"
    )
)

if not exist "%OUTPUT%" (
    mkdir "%OUTPUT%" || (
        echo [wil2png] Failed to create output directory: %OUTPUT%
        exit /b 1
    )
)

echo [wil2png] Input:  %INPUT%
echo [wil2png] Output: %OUTPUT%

if "%MODE%"=="batch" (
    "%EXE%" "%INPUT%" -o "%OUTPUT%" --skip-empty --batch
) else (
    "%EXE%" "%INPUT%" -o "%OUTPUT%" --skip-empty
)

echo.
echo Done. Press any key to exit.
pause >nul
