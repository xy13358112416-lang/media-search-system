@echo off
chcp 65001 >nul
"%~dp0media_server.exe" --config "%~dp0media-server.ini"
if errorlevel 1 pause
