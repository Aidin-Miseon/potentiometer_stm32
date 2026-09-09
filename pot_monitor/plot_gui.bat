@echo off
title STM32 waveform viewer  [pot_monitor 1000Hz, display 100Hz]
powershell -NoLogo -ExecutionPolicy Bypass -File "%~dp0tools\plot_gui.ps1" -SampleHz 1000
if errorlevel 1 pause
