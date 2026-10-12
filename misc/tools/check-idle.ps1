<#
check-idle.ps1 - measure idle CPU / GPU usage of the running ULin Hub window.

WHY THIS EXISTS
  An infinite animation on an invisible item keeps marking the scene dirty, so the
  app redraws at full rate even when idle. Measured 2026-09-30 (wallpaper skin active,
  Shell.qml BusySpinner not gated):
      CPU 0.813 s per 15 s  = 5.4 % of one core
      GPU 3D engine          = 25 .. 29 %
  After gating the spinner (running: Hub.initializing) both should drop to ~0.

USAGE
  1) start the app and LEAVE IT ALONE (do not click / move it)
  2) double-click misc\tools\check-idle.cmd   (recommended: the execution policy on this
     machine blocks unsigned .ps1 files), or run it from a terminal:
       powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\check-idle.ps1
     to make sure you are not measuring a stale instance, add the patch time:
       powershell -NoProfile -ExecutionPolicy Bypass -File misc\tools\check-idle.ps1 -Since "2026-10-01 00:00:38"

  -Since <datetime>  : abort if the process started BEFORE this time (stale instance guard)

EXIT CODES
  0 = looks idle (verdict PASS)
  1 = still redrawing while idle (verdict FAIL)
  2 = no process / stale instance / cannot measure

NOTE: comments are intentionally ASCII-only - .ps1 files without a BOM are read as ANSI
      by Windows PowerShell 5.1 and Chinese comments would break parsing.
#>
param(
    [int]$Seconds = 15,
    [datetime]$Since,
    [string]$ProcessName = 'ULin Hub'
)

$ErrorActionPreference = 'Stop'

$p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if (-not $p) {
    Write-Host "[check-idle] no '$ProcessName' process - start the app first." -ForegroundColor Yellow
    exit 2
}
$proc = $p | Sort-Object StartTime | Select-Object -First 1

Write-Host ("[check-idle] pid={0}  started={1}  threads={2}" -f $proc.Id, $proc.StartTime, $proc.Threads.Count)
if ($Since -and $proc.StartTime -lt $Since) {
    Write-Host ("[check-idle] STALE INSTANCE: started {0}, which is before -Since {1}." -f $proc.StartTime, $Since)
    Write-Host "[check-idle] close it completely, relaunch, then measure again." -ForegroundColor Yellow
    exit 2
}

Write-Host ("[check-idle] sampling CPU for {0}s - do not touch the window ..." -f $Seconds)
$t1 = (Get-Process -Id $proc.Id).CPU
Start-Sleep -Seconds $Seconds
$t2 = (Get-Process -Id $proc.Id).CPU
$delta = $t2 - $t1
$cpuPct = 100.0 * $delta / $Seconds

$gpu = @()
try {
    for ($i = 1; $i -le 3; $i++) {
        $samples = (Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction Stop).CounterSamples |
            Where-Object { $_.InstanceName -like "*pid_$($proc.Id)_*" -and $_.InstanceName -like '*engtype_3D*' }
        $sum = 0.0
        foreach ($s in $samples) { $sum += $s.CookedValue }
        if ($samples) { $gpu += $sum }
        Start-Sleep -Milliseconds 800
    }
} catch {
    Write-Host "[check-idle] GPU counters unavailable, CPU only." -ForegroundColor DarkGray
}

Write-Host ""
Write-Host ("  CPU : {0:N3} s over {1}s  =>  {2:N2} % of one core   (was 5.40 % before the fix)" -f $delta, $Seconds, $cpuPct)
if ($gpu.Count -gt 0) {
    Write-Host ("  GPU : 3D engine {0}   (was 25..29 % before the fix)" -f (($gpu | ForEach-Object { "{0:N1} %" -f $_ }) -join ' / '))
}

$cpuOk = $delta -lt (0.15 * $Seconds / 15.0)
$gpuAvg = if ($gpu.Count -gt 0) { ($gpu | Measure-Object -Average).Average } else { 0 }
$gpuOk = ($gpu.Count -eq 0) -or ($gpuAvg -lt 5.0)

Write-Host ""
if ($cpuOk -and $gpuOk) {
    Write-Host "[check-idle] PASS - the app is idle, it is not redrawing continuously." -ForegroundColor Green
    exit 0
}
Write-Host "[check-idle] FAIL - still redrawing while idle. Something is animating; check for an" -ForegroundColor Red
Write-Host "             infinite animation that is not gated (BusySpinner.running / loops: Animation.Infinite)."
exit 1
