# Artec scan-slowdown diagnostic. READ-ONLY: changes nothing on the laptop.
# Usage (PowerShell, run as normal user):
#   .\scan-diag.ps1 -Mode Snapshot     once, now (system facts, drivers, event log)
#   .\scan-diag.ps1 -Mode Log          start BEFORE scanning, leave running, Ctrl+C when done
# Output goes to C:\ArtecDiag\<timestamp>\ . Small files. Copy the folder back on a USB stick.
param([ValidateSet('Snapshot','Log')][string]$Mode='Snapshot', [int]$Seconds=1, [string]$Root='D:\ArtecDiag')
$out = "$Root\" +(Get-Date -Format 'yyyyMMdd-HHmmss'); New-Item -ItemType Directory -Force $out | Out-Null

if ($Mode -eq 'Snapshot') {
  Get-CimInstance Win32_ComputerSystem | Format-List * | Out-File "$out\system.txt"
  Get-CimInstance Win32_BIOS | Select Manufacturer,SMBIOSBIOSVersion,ReleaseDate | Format-List | Out-File "$out\system.txt" -Append
  Get-CimInstance Win32_OperatingSystem | Select Caption,Version,BuildNumber,InstallDate | Format-List | Out-File "$out\system.txt" -Append
  Get-CimInstance Win32_Processor | Select Name,MaxClockSpeed,NumberOfCores | Format-List | Out-File "$out\system.txt" -Append
  Get-CimInstance Win32_VideoController | Select Name,DriverVersion,DriverDate | Format-List | Out-File "$out\gpu.txt"
  # Drivers: USB, Thunderbolt, chipset, Artec
  Get-CimInstance Win32_PnPSignedDriver | ? { $_.DeviceName -match 'USB|Thunderbolt|xHCI|Artec|Chipset|Management Engine|Dock|HP' } |
    Select DeviceName,DriverVersion,DriverDate,Manufacturer | Sort DeviceName | Format-Table -Auto | Out-String -Width 250 | Out-File "$out\drivers-usb-tb.txt"
  Get-PnpDevice -PresentOnly | ? { $_.Class -match 'USB|Net|HIDClass' -or $_.FriendlyName -match 'Artec|Thunderbolt' } |
    Select Status,Class,FriendlyName,InstanceId | Format-Table -Auto | Out-String -Width 300 | Out-File "$out\pnp-present.txt"
  powercfg /getactivescheme | Out-File "$out\power.txt"
  powercfg /query SCHEME_CURRENT SUB_PROCESSOR | Out-File "$out\power.txt" -Append
  powercfg /query SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 | Out-File "$out\power.txt" -Append   # USB selective suspend
  Get-HotFix | Sort InstalledOn -Descending | Select -First 25 HotFixID,InstalledOn,Description | Format-Table -Auto | Out-File "$out\hotfixes.txt"
  Get-ChildItem 'C:\Program Files\Artec' -ErrorAction SilentlyContinue | Select Name,LastWriteTime | Out-File "$out\artec-installed.txt"
  Get-CimInstance Win32_Battery | Select EstimatedChargeRemaining,BatteryStatus,DesignCapacity,FullChargeCapacity | Format-List | Out-File "$out\battery.txt"
  powercfg /batteryreport /output "$out\battery-report.html" | Out-Null
  # Last 60 days of relevant events
  $since = (Get-Date).AddDays(-60)
  Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -ErrorAction SilentlyContinue |
    ? { $_.ProviderName -match 'WHEA|Kernel-Power|Kernel-PnP|USB|Thunderbolt|nvlddmkm|Display|Kernel-Processor-Power|Disk|stornvme' -and $_.Level -le 3 } |
    Select TimeCreated,ProviderName,Id,LevelDisplayName,@{n='Msg';e={($_.Message -split "`n")[0]}} |
    Export-Csv "$out\events-system.csv" -NoTypeInformation
  Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$since} -ErrorAction SilentlyContinue |
    ? { $_.ProviderName -match 'Artec|Application Error|Windows Error' -and $_.Message -match 'Artec' } |
    Select TimeCreated,ProviderName,Id,@{n='Msg';e={($_.Message -split "`n")[0]}} | Export-Csv "$out\events-artec.csv" -NoTypeInformation
  if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) { nvidia-smi -q | Out-File "$out\nvidia-smi-q.txt" }
  "Snapshot written to $out"; return
}

# ---- Log mode: one row per second while you scan ----
$csv = "$out\scan-log.csv"
$counters = @(
 '\Processor Information(_Total)\% Processor Utility',
 '\Processor Information(_Total)\% Processor Performance',
 '\Processor Information(_Total)\Processor Frequency',
 '\Processor Information(_Total)\% DPC Time',
 '\Processor Information(_Total)\% Interrupt Time',
 '\Processor Information(_Total)\DPCs Queued/sec',
 '\Memory\Available MBytes','\Memory\Pages/sec',
 '\PhysicalDisk(_Total)\% Disk Time',
 '\Thermal Zone Information(*)\Temperature',
 '\Thermal Zone Information(*)\% Passive Limit'
)
$hasSmi = [bool](Get-Command nvidia-smi -ErrorAction SilentlyContinue)
"Logging to $csv . Start your scan now. Ctrl+C to stop." | Write-Host
while ($true) {
  $row = [ordered]@{ time=(Get-Date -Format 'HH:mm:ss'); onAC=((Get-CimInstance -Namespace root\wmi -ClassName BatteryStatus -EA SilentlyContinue | select -First 1).PowerOnline) }
  try {
    (Get-Counter -Counter $counters -ErrorAction SilentlyContinue).CounterSamples | % {
      $n = ($_.Path -replace '^\\\\[^\\]+\\','') -replace '[^A-Za-z0-9_%()\\ ]',''; $row[$n] = [math]::Round($_.CookedValue,1) }
  } catch {}
  if ($hasSmi) { $g = (nvidia-smi --query-gpu=temperature.gpu,clocks.sm,clocks.max.sm,utilization.gpu,power.draw,clocks_throttle_reasons.active --format=csv,noheader,nounits 2>$null)
                 if ($g) { $row['gpu_temp_clk_maxclk_util_pwr_throttle'] = $g.Trim() } }
  $p = Get-Process -EA SilentlyContinue | ? { $_.Name -match 'Artec' } | Select -First 1
  if ($p) { $row['artec_cpu_s'] = [math]::Round($p.CPU,1); $row['artec_ws_mb'] = [math]::Round($p.WorkingSet64/1MB) }
  [pscustomobject]$row | Export-Csv $csv -Append -NoTypeInformation
  Start-Sleep -Seconds $Seconds
}
