@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0wifi_provision.ps1" %*
pause
