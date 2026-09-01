@echo off
title STM32 COM monitor
powershell -NoLogo -ExecutionPolicy Bypass -File "%~dp0tools\read_com.ps1"
pause
