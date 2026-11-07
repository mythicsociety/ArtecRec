# Copies the listed finished projects (project file + Scans + smallest stl/ply export) to D:\ArtecReplay\finished.
# Read-only on the NAS. Usage: finished-copy2.ps1 [list file]
param([string]$List = "$PSScriptRoot\finished-list.txt")
$root = '\\192.168.0.18\AllProjectData\3D_data\FinishedModels'; $dst = 'D:\ArtecReplay\finished'; $log = "$PSScriptRoot\finished-copy.log"
New-Item -ItemType Directory -Force $dst | Out-Null; $t0 = Get-Date; $n = 0
foreach ($rel in Get-Content $List) {
  $rel = $rel.Trim(); if (-not $rel) { continue }
  $n++
  $relDir = Split-Path $rel -Parent; $leaf = Split-Path $rel -Leaf
  $srcDir = Join-Path $root $relDir
  $name = ((Split-Path $srcDir -Leaf) + '__' + [IO.Path]::GetFileNameWithoutExtension($leaf)) -replace '[^\w\.-]', '_'
  $to = Join-Path $dst $name
  $ts = Get-Date
  "   $n src=[$srcDir] exists=$(Test-Path $srcDir) sproj=$(Test-Path (Join-Path $srcDir $leaf)) to=[$to]" | Add-Content $log
  & robocopy.exe "$srcDir" "$to" "$leaf" /R:3 /W:10 /NP /NFL /NDL /NJH /NJS | Out-Null; $rc1 = $LASTEXITCODE
  & robocopy.exe "$srcDir\Scans" "$to\Scans" /E /R:3 /W:10 /NP /NFL /NDL /NJH /NJS | Out-Null; $rc = $LASTEXITCODE
  $ex = Get-ChildItem "$srcDir\Exports" -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Extension -match '^\.(stl|ply)$' } | Sort-Object Length | Select-Object -First 1
  if ($ex) { New-Item -ItemType Directory -Force "$to\Model" | Out-Null; & robocopy.exe "$($ex.DirectoryName)" "$to\Model" "$($ex.Name)" /R:3 /W:10 /NP /NFL /NDL /NJH /NJS | Out-Null }
  $got = (Get-ChildItem $to -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1MB
  "$n $name : $([math]::Round($got)) MB (model $(if ($ex) { $ex.Name } else { 'none' })), robocopy $rc1/$rc, $([int]((Get-Date)-$ts).TotalSeconds) s, elapsed $([int]((Get-Date)-$t0).TotalMinutes) min" | Add-Content $log
}
"DONE: $n projects in $([int]((Get-Date)-$t0).TotalMinutes) min" | Add-Content $log
