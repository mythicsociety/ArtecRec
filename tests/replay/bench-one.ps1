# Replay benchmark of one copied project (mid-pass resume), with the saved-position and finished-model checks.
# Usage: bench-one.ps1 <project folder under D:\ArtecReplay\finished> [csv name]
param([string]$Folder, [string]$Csv = 'bench-finished.csv', [int]$MaxMin = 25, [string]$Mode = 'mid', [string]$Bin = 'D:\ArtecReplay\bin', [string]$Exe = 'C:\Artec\sdk-fps\artecrec.exe')
$modeArg = switch ($Mode) { 'mid' { '--replay-from-mid' } 'overlap' { '--replay-from-overlap' } default { '' } }
$env:PATH = "C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64;" + $env:PATH
$bin = $Bin; $res = 'D:\ArtecReplay\results'; New-Item -ItemType Directory -Force $bin, $res | Out-Null
try { Copy-Item $Exe $bin -Force -ErrorAction Stop } catch { "WARNING: could not refresh $bin\artecrec.exe (in use?): $($_.Exception.Message)" }
Copy-Item ($Exe + '.manifest') $bin -Force -ErrorAction SilentlyContinue
$pr = Get-ChildItem $Folder -Filter *.sproj -File | Select-Object -First 1; if (-not $pr) { "no .sproj in $Folder"; exit 1 }
$tag = "fin-$Mode-" + ((Split-Path $Folder -Leaf) -replace '[^\w-]', '_'); $ts = Get-Date
$p = Start-Process "$bin\artecrec.exe" -ArgumentList "--replay-bench $res\$Csv $modeArg `"$($pr.FullName)`"" -WorkingDirectory $bin -PassThru
$p.WaitForExit($MaxMin * 60000) | Out-Null; $killed = $false; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $killed = $true }
"$(Get-Date -Format 'HH:mm') $tag : $([int]((Get-Date)-$ts).TotalSeconds) s$(if ($killed) { ' KILLED (timeout)' })" | Add-Content "$res\driver.log"
Copy-Item "$bin\artecrec.log" "$res\log-$tag.txt" -Force -ErrorAction SilentlyContinue
Get-Content "$bin\artecrec.log" | Select-String 'finished model|replay bench: saved|replay bench done|fitted|anchor' | Select-Object -Last 12
Get-Content "$res\$Csv" | Select-String ([regex]::Escape($pr.Name)) | Select-Object -Last 12
