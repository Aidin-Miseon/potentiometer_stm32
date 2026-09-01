@echo off
title STM32 waveform viewer  [pot_monitor]
powershell -NoLogo -ExecutionPolicy Bypass -File "%~dp0tools\plot_gui.ps1"
if errorlevel 1 pause
