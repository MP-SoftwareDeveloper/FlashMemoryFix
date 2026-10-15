@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0usb_diag.ps1"
echo Done - wrote captures\usb_diag.txt
pause
