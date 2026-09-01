<#
  STM32 실시간 파형 뷰어 (MATLAB 스타일, WinForms Chart)
  - COM 포트에서 raw0/flt0/raw1/flt1을 읽어 두 개의 좌표축에 라인으로 그린다.
  - 파랑 = 원본, 주황 = 필터.  최근 -Window 초(기본 10초)만 슬라이딩 표시, Y축 자동 스케일.
  - 구버전 펌웨어(raw만 출력)도 동작 (필터선 = 원본선).
  사용:  plot_gui.ps1            (창 닫으면 종료)
         plot_gui.ps1 -Window 30 (30초 폭)
#>
param(
  [string]$Port = "",
  [int]$Window = 10,       # 표시 구간 [초]
  [int]$Seconds = 0,       # 0=계속, N초 후 자동 종료(테스트용)
  [switch]$Noise           # 시작부터 "노이즈만 보기" 모드
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Windows.Forms.DataVisualization
Add-Type -AssemblyName System.Drawing

if ($Port -eq "") {
  $dev = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match 'VID_0483&PID_5740' } | Select-Object -First 1
  if ($null -eq $dev) { [System.Windows.Forms.MessageBox]::Show("STM32 USB 직렬 장치를 찾지 못했습니다.`n보드 연결 / BOOT0 Low / 리셋을 확인하세요.","plot_gui") | Out-Null; exit 1 }
  $Port = $dev.DeviceID
}

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.DtrEnable = $true; $sp.RtsEnable = $true
try { $sp.Open() } catch {
  [System.Windows.Forms.MessageBox]::Show("$Port 를 열 수 없습니다:`n$($_.Exception.Message)`n(read/plot 창이 열려 있으면 닫아 주세요)","plot_gui") | Out-Null; exit 1
}

$CH = 'System.Windows.Forms.DataVisualization.Charting'
$chart = New-Object "$CH.Chart"
$chart.Dock = 'Fill'; $chart.BackColor = 'White'

$colRaw  = [System.Drawing.Color]::FromArgb(0,114,189)    # MATLAB 파랑
$colFlt  = [System.Drawing.Color]::FromArgb(217,83,25)    # MATLAB 주황
$colGrid = [System.Drawing.Color]::Gainsboro

foreach ($nm in @('A0','A1')) {
  $a = New-Object "$CH.ChartArea" $nm
  $a.BackColor = 'White'
  $a.AxisX.MajorGrid.LineColor = $colGrid; $a.AxisY.MajorGrid.LineColor = $colGrid
  $a.AxisX.LineColor = 'Black'; $a.AxisY.LineColor = 'Black'
  $a.AxisX.LabelStyle.Format = '0.0'
  $a.AxisX.Title = '시간 [s]  (100 Hz 기준)'
  $a.AxisY.Title = if ($nm -eq 'A0') { 'PA0 [카운트]' } else { 'PA1 [카운트]' }
  $a.AxisX.TitleFont = New-Object System.Drawing.Font('Segoe UI',9)
  $a.AxisY.TitleFont = New-Object System.Drawing.Font('Segoe UI',9)
  $chart.ChartAreas.Add($a)
}
$leg = New-Object "$CH.Legend"; $leg.Docking = 'Top'; $chart.Legends.Add($leg)

function AddSeries([string]$name, [string]$area, $color, [int]$width) {
  $s = New-Object "$CH.Series" $name
  $s.ChartType = 'FastLine'; $s.ChartArea = $area; $s.Color = $color; $s.BorderWidth = $width
  $s.XValueType = 'Double'
  $script:chart.Series.Add($s); $s
}
$colNoise = [System.Drawing.Color]::FromArgb(126,47,142)  # MATLAB 보라
$sR0 = AddSeries 'PA0 원본' 'A0' $colRaw 1
$sF0 = AddSeries 'PA0 필터' 'A0' $colFlt 2
$sN0 = AddSeries 'PA0 노이즈' 'A0' $colNoise 1
$sR1 = AddSeries 'PA1 원본' 'A1' $colRaw 1
$sF1 = AddSeries 'PA1 필터' 'A1' $colFlt 2
$sN1 = AddSeries 'PA1 노이즈' 'A1' $colNoise 1

$form = New-Object System.Windows.Forms.Form
$form.Text = "STM32 실시간 파형  [$Port]  파랑=원본  주황=필터  보라=노이즈"
$form.Width = 1000; $form.Height = 720; $form.BackColor = 'White'

$chk = New-Object System.Windows.Forms.CheckBox
$chk.Text = '노이즈만 보기 (원본 - 필터, 0 중심)'
$chk.AutoSize = $true; $chk.Left = 12; $chk.Top = 7; $chk.BackColor = 'White'
$panel = New-Object System.Windows.Forms.Panel
$panel.Height = 32; $panel.Dock = 'Top'; $panel.BackColor = 'White'
$panel.Controls.Add($chk)

function ApplyMode {
  $on = $chk.Checked
  $sR0.Enabled = -not $on; $sF0.Enabled = -not $on; $sN0.Enabled = $on
  $sR1.Enabled = -not $on; $sF1.Enabled = -not $on; $sN1.Enabled = $on
  if (-not $on) {
    $chart.ChartAreas['A0'].AxisY.Title = 'PA0 [카운트]'
    $chart.ChartAreas['A1'].AxisY.Title = 'PA1 [카운트]'
  }
}
$chk.Add_CheckedChanged({ ApplyMode })
if ($Noise) { $chk.Checked = $true }
ApplyMode

$form.Controls.Add($chart)
$form.Controls.Add($panel)

$script:acc = ''      # 시리얼 수신 버퍼 (줄 조립용)
$script:n   = 0       # 샘플 카운터 → t = n/100

$script:tick = 0
function TrimAndScale([string]$area, $list, [string]$chName) {
  $tMax = $script:n / 100.0
  $tMin = [Math]::Max(0, $tMax - $Window)
  foreach ($s in $list) { while ($s.Points.Count -gt 0 -and $s.Points[0].XValue -lt $tMin) { $s.Points.RemoveAt(0) } }
  $vis = @($list | Where-Object { $_.Enabled })
  if ($vis.Count -eq 0 -or $vis[0].Points.Count -lt 2) { return }
  $a = $chart.ChartAreas[$area]
  $a.AxisX.Minimum = $tMin; $a.AxisX.Maximum = [Math]::Max($tMin + 1, $tMax)
  $lo = [double]::MaxValue; $hi = [double]::MinValue
  foreach ($s in $vis) { foreach ($pt in $s.Points) { $v = $pt.YValues[0]; if ($v -lt $lo) { $lo = $v }; if ($v -gt $hi) { $hi = $v } } }
  $pad = [Math]::Max(5, ($hi - $lo) * 0.15)
  $a.AxisY.Minimum = [Math]::Floor($lo - $pad); $a.AxisY.Maximum = [Math]::Ceiling($hi + $pad)

  # 노이즈 모드: 0.5초마다 창 안의 표준편차를 Y축 제목에 표시
  if ($chk.Checked -and ($script:tick % 10 -eq 0)) {
    $pts = $vis[0].Points
    if ($pts.Count -ge 10) {
      $m = 0.0; foreach ($pt in $pts) { $m += $pt.YValues[0] }; $m /= $pts.Count
      $v = 0.0; foreach ($pt in $pts) { $d = $pt.YValues[0] - $m; $v += $d * $d }
      $sd = [Math]::Sqrt($v / ($pts.Count - 1))
      $a.AxisY.Title = ('{0} 노이즈 [카운트]   σ = {1:F2}' -f $chName, $sd)
    }
  }
}

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 50
$timer.Add_Tick({
  try { $data = $sp.ReadExisting() } catch { return }
  if ([string]::IsNullOrEmpty($data)) { return }
  $script:acc += $data
  $lines = $script:acc -split "`n"
  $script:acc = $lines[-1]                      # 마지막 조각은 미완성 줄
  for ($i = 0; $i -lt $lines.Count - 1; $i++) {
    $line = $lines[$i]
    if ($line -match 'raw0=\s*(\d+)\s+flt0=\s*(\d+).*raw1=\s*(\d+)\s+flt1=\s*(\d+)') {
      $r0=[double]$Matches[1]; $f0=[double]$Matches[2]; $r1=[double]$Matches[3]; $f1=[double]$Matches[4]
    } elseif ($line -match 'raw0=\s*(\d+).*raw1=\s*(\d+)') {
      $r0=[double]$Matches[1]; $f0=$r0; $r1=[double]$Matches[2]; $f1=$r1
    } else { continue }
    $t = $script:n / 100.0; $script:n++
    [void]$sR0.Points.AddXY($t, $r0); [void]$sF0.Points.AddXY($t, $f0); [void]$sN0.Points.AddXY($t, $r0 - $f0)
    [void]$sR1.Points.AddXY($t, $r1); [void]$sF1.Points.AddXY($t, $f1); [void]$sN1.Points.AddXY($t, $r1 - $f1)
  }
  $script:tick++
  TrimAndScale 'A0' @($sR0, $sF0, $sN0) 'PA0'
  TrimAndScale 'A1' @($sR1, $sF1, $sN1) 'PA1'
})

if ($Seconds -gt 0) {
  $quit = New-Object System.Windows.Forms.Timer
  $quit.Interval = $Seconds * 1000
  $quit.Add_Tick({ $form.Close() })
  $quit.Start()
}

$form.Add_Shown({ $sp.DiscardInBuffer(); $timer.Start() })
$form.Add_FormClosed({ $timer.Stop(); if ($sp.IsOpen) { $sp.Close() } })
[System.Windows.Forms.Application]::Run($form)
"closed"
