@echo off
REM ============================================================
REM  pot_monitor : build + flash via USB DFU
REM  1) Builds the project with headless STM32CubeIDE (separate
REM     workspace .headless_ws - safe while the IDE is open)
REM  2) Flashes the elf over USB DFU
REM  Before: BOOT0 = High + reset (DFU). After: BOOT0 = Low + replug.
REM ============================================================
setlocal
set "IDE=C:\ST\STM32CubeIDE_2.1.1\STM32CubeIDE\stm32cubeidec.exe"
set "CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
set "WS=%~dp0..\.headless_ws"
set "PROJDIR=%~dp0."
set "ELF=%~dp0Debug\pot_monitor.elf"
set "LOG=%TEMP%\hb_pot_monitor.log"
title STM32 build+flash  [pot_monitor]

echo [1/2] Building pot_monitor ... (about 30 s; STM32CubeIDE may stay open)
"%IDE%" --launcher.suppressErrors -nosplash -application org.eclipse.cdt.managedbuilder.core.headlessbuild -data "%WS%" -import "%PROJDIR%" -build pot_monitor/Debug > "%LOG%" 2>&1
findstr /C:"Build Finished. 0 errors" "%LOG%" >nul
if errorlevel 1 (
  echo [ERROR] Build FAILED. Error lines:
  findstr /I /C:"error" "%LOG%"
  echo Full log: %LOG%
  pause
  exit /b 1
)
findstr /C:"Build Finished" "%LOG%"
if not exist "%ELF%" (
  echo [ERROR] %ELF% not found after build.
  pause
  exit /b 1
)
for %%F in ("%ELF%") do echo ELF : %%~tF   %%~zF bytes
echo.

echo [2/2] Checking for a board in DFU mode ...
"%CLI%" -l | findstr /C:"STM32  BOOTLOADER" >nul
if errorlevel 1 (
  echo [ERROR] No STM32 in DFU mode. Set BOOT0=High, reset the board, then run this again.
  pause
  exit /b 1
)
echo DFU device found. Programming ...
echo.
"%CLI%" -c port=USB1 -w "%ELF%" -v -g 0x08000000
if errorlevel 1 (
  echo.
  echo [ERROR] Programming failed. See messages above.
  pause
  exit /b 1
)
echo.
echo ============================================================
echo  Done. Now: BOOT0 = Low, replug USB, then run read_com.bat
echo ============================================================
pause