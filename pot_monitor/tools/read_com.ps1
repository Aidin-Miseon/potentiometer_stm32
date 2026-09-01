<#
  STM32 USB 가상 COM 포트 모니터
  - 포트를 지정하지 않으면 STM32 CDC 장치(VID_0483&PID_5740)를 자동으로 찾는다.
  - 기본 모드 : 키 입력을 보드로 전달  (c = 최대/최소 캘리브레이션, i = 보드 정보)
  - -Simple   : 출력만 보는 모드 (v1 펌웨어용, 키 전달 없음)
  - 종료: Ctrl+C
  사용 예:
    powershell -ExecutionPolicy Bypass -File read_com.ps1
    powershell -ExecutionPolicy Bypass -File read_com.ps1 -Simple
    powershell -ExecutionPolicy Bypass -File read_com.ps1 -Port COM3
#>
param(
  [string]$Port = "",
  [int]$Seconds = 0,         # 0 = 무한, 테스트용으로 N초만 읽고 종료
  [switch]$Simple            # 키 전달 없이 출력만
)

if ($Port -eq "") {
  $dev = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match 'VID_0483&PID_5740' } | Select-Object -First 1
  if ($null -eq $dev) {
    Write-Host "STM32 USB 직렬 장치를 찾지 못했습니다. 보드 연결 / BOOT0 Low / 리셋을 확인하세요." -ForegroundColor Red
    Write-Host "현재 포트 목록: $([System.IO.Ports.SerialPort]::GetPortNames() -join ', ')"
    exit 1
  }
  $Port = $dev.DeviceID
}

$p = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$p.ReadTimeout = 200
$p.DtrEnable   = $true
$p.RtsEnable   = $true
$p.NewLine     = "`n"

# 콘솔이 없거나 입력이 리다이렉트된 경우(자동 실행 등)에는 키 입력 기능을 끈다
$keysOk = -not $Simple
if ($keysOk) { try { $null = [Console]::KeyAvailable } catch { $keysOk = $false } }

try {
  $p.Open()
  if ($Simple) {
    Write-Host "[$Port] 연결됨.   (v1: 출력 전용)   Ctrl+C = 종료" -ForegroundColor Green
  } else {
    Write-Host "[$Port] 연결됨.   c=둘 다 캘리브  0=PA0만  1=PA1만  i=보드 정보  Ctrl+C=종료" -ForegroundColor Green
  }
  $p.DiscardInBuffer()
  $sw = [System.Diagnostics.Stopwatch]::StartNew()

  while (($Seconds -le 0) -or ($sw.Elapsed.TotalSeconds -lt $Seconds)) {
    # 1) 보드 → 화면
    try {
      $line = $p.ReadLine().TrimEnd("`r")
      if ($line.StartsWith("CALIB")) {
        Write-Host $line -ForegroundColor Yellow
      } elseif ($line.StartsWith("info")) {
        Write-Host $line -ForegroundColor Cyan
      } else {
        Write-Host $line
      }
    }
    catch [System.TimeoutException] {
      # 200 ms 동안 데이터 없음 - 계속 대기
    }

    # 2) 키보드 → 보드
    if ($keysOk) {
      try {
        while ([Console]::KeyAvailable) {
          $k = [Console]::ReadKey($true)
          if ($k.KeyChar -ne [char]0) { $p.Write([string]$k.KeyChar) }
        }
      } catch { $keysOk = $false }
    }
  }
}
catch {
  Write-Host "오류: $($_.Exception.Message)" -ForegroundColor Red
  Write-Host "다른 프로그램이 $Port 를 이미 열고 있으면 먼저 닫아 주세요."
  exit 1
}
finally {
  if ($p.IsOpen) { $p.Close() }
}
