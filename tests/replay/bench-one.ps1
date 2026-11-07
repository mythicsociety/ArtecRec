# Replay benchmark of one copied project (mid-pass resume), with the saved-position and finished-model checks.
# Usage: bench-one.ps1 <project folder under D:\ArtecReplay\finished> [csv name]
param([string]$Folder, [string]$Csv = 'bench-finished.csv', [int]$MaxMin = 25)
$env:PATH = "C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64;" + $env:PATH
$bin = 'D:\ArtecReplay\bin'; $res = 'D:\ArtecReplay\results'; New-Item -ItemType Directory -Force $bin, $res | Out-Null
Copy-Item C:\Artec\sdk-fps\artecrec.exe $bin -Force; Copy-Item C:\Artec\sdk-fps\artecrec.exe.manifest $bin -Force -ErrorAction SilentlyContinue
$pr = Get-ChildItem $Folder -Filter *.sproj -File | Select-Object -First 1; if (-not $pr) { "no .sproj in $Folder"; exit 1 }
$tag = 'fin-' + ((Split-Path $Folder -Leaf) -replace '[^\w-]', '_'); $ts = Get-Date
$p = Start-Process "$bin\artecrec.exe" -ArgumentList "--replay-bench $res\$Csv --replay-from-mid `"$($pr.FullName)`"" -WorkingDirectory $bin -PassThru
$p.WaitForExit($MaxMin * 60000) | Out-Null; $killed = $false; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $killed = $true }
"$(Get-Date -Format 'HH:mm') $tag : $([int]((Get-Date)-$ts).TotalSeconds) s$(if ($killed) { ' KILLED (timeout)' })" | Add-Content "$res\driver.log"
Copy-Item "$bin\artecrec.log" "$res\log-$tag.txt" -Force -ErrorAction SilentlyContinue
Get-Content "$bin\artecrec.log" | Select-String 'finished model|replay bench: saved|replay bench done|fitted|anchor' | Select-Object -Last 12
Get-Content "$res\$Csv" | Select-String ([regex]::Escape($pr.Name)) | Select-Object -Last 12
