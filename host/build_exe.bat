@echo off
rem ---------------------------------------------------------------------------
rem Build a portable ACS37610 Programmer for Windows (no Python/VS Code needed
rem on the target PC). Output: dist\ACS37610-Programmer\ACS37610-Programmer.exe
rem — copy that whole folder to any Windows 10/11 x64 machine and run the exe.
rem
rem One-time prerequisite (in this host\ directory):
rem     .venv\Scripts\python.exe -m pip install pyinstaller
rem ---------------------------------------------------------------------------
cd /d "%~dp0"

.venv\Scripts\python.exe -m PyInstaller ^
    --noconfirm --clean --windowed ^
    --name ACS37610-Programmer ^
    acs_programmer.py
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo Build OK: dist\ACS37610-Programmer\ACS37610-Programmer.exe
echo Distribute by copying the entire dist\ACS37610-Programmer folder.
