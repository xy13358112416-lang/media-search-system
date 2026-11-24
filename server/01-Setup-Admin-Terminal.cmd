@echo off
chcp 65001 >nul
"%~dp0media_server.exe" --config "%~dp0media-server.ini" --setup-admin
pause
