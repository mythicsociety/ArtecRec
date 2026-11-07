# Benchmarks (overlap mode) the five projects already on D:, then each batch-2 project as its copy lands.
param([string]$List = "$PSScriptRoot\finished-list-2.txt", [string]$Csv = 'bench-overlap2.csv', [switch]$SkipDone)
$S = $PSScriptRoot; $log = "$S\finished-copy.log"; $dst = 'D:\ArtecReplay\finished'
$names = @(Get-ChildItem $dst -Directory | Select-Object -ExpandProperty Name)
if ($SkipDone) {   # projects whose rows are already in the CSV are not run again
  $done = @(); if (Test-Path "D:\ArtecReplay\results\$Csv") { $done = Get-Content "D:\ArtecReplay\results\$Csv" | ForEach-Object { ($_ -split ',')[0] } }
  $names = @($names | Where-Object { $sp = Get-ChildItem "$dst\$_" -Filter *.sproj -File | Select-Object -First 1; -not $sp -or ($done -notcontains $sp.Name) }) }
$pending = @(); foreach ($rel in Get-Content $List) { $rel = $rel.Trim(); if (-not $rel) { continue }
  $relDir = Split-Path $rel -Parent; $leaf = Split-Path $rel -Leaf; $n = ((Split-Path $relDir -Leaf) + '__' + [IO.Path]::GetFileNameWithoutExtension($leaf)) -replace '[^\w\.-]', '_'; if ($names -notcontains $n) { $pending += $n } }
foreach ($n in $names) { "$(Get-Date -Format 'HH:mm') bench2 $n" | Add-Content "D:\ArtecReplay\results\driver.log"; & "$S\bench-one.ps1" -Folder "$dst\$n" -Csv $Csv -Mode overlap 2>&1 | Out-String | Add-Content "D:\ArtecReplay\results\finished-bench2.out" }
foreach ($n in $pending) {
  while (-not (Select-String -Path $log -Pattern (" " + [regex]::Escape($n) + " : .*robocopy [01]/") -Quiet)) { Start-Sleep 30 }
  "$(Get-Date -Format 'HH:mm') bench2 $n" | Add-Content "D:\ArtecReplay\results\driver.log"
  & "$S\bench-one.ps1" -Folder "$dst\$n" -Csv $Csv -Mode overlap 2>&1 | Out-String | Add-Content "D:\ArtecReplay\results\finished-bench2.out"
}
"ALL-DONE-2 $(Get-Date -Format 'HH:mm')" | Add-Content "D:\ArtecReplay\results\driver.log"
