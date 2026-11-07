# Field mode (7 Nov, owner): with fieldMode=1 in artecrec.ini a replayed field day must record and save only -
# no surface optimisation, no refinement, no auto-align after Stop; the live search still joins; passes are autosaved.
# Usage: field-mode-check.ps1 [-Folder <finished project with 3 scans>] [-Exe C:\Artec\sdk-fps\artecrec.exe]
param([string]$Folder = (Get-ChildItem 'D:\ArtecReplay\finished' -Directory -Filter '*Maranay*' | Select-Object -First 1).FullName,
      [string]$Exe = 'C:\Artec\sdk-fps\artecrec.exe', [string]$Script = 'C:\Artec\tests\e2e\replay-field-day.txt', [int]$MaxMin = 15)
$env:PATH = "C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64;" + $env:PATH
$bin = 'D:\ArtecReplay\bin-field'; New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item $Exe $bin -Force; Copy-Item ($Exe + '.manifest') $bin -Force -ErrorAction SilentlyContinue
"[scan]`r`niniVersion=2`r`nfieldMode=1`r`nautoAlign=1`r`nscanUsingAutoAlign=1`r`n" | Set-Content "$bin\artecrec.ini" -Encoding Unicode
Remove-Item "$bin\artecrec.log" -ErrorAction SilentlyContinue
$pr = Get-ChildItem $Folder -Filter *.sproj -File | Select-Object -First 1; if (-not $pr) { "no .sproj in $Folder"; exit 2 }
$p = Start-Process "$bin\artecrec.exe" -ArgumentList "--replay-script `"$Script`" `"$($pr.FullName)`"" -WorkingDirectory $bin -PassThru
$p.WaitForExit($MaxMin * 60000) | Out-Null; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "KILLED (timeout)" }
$log = Get-Content "$bin\artecrec.log"
$fail = 0
function Expect($name, $ok) { if ($ok) { "PASS  $name" } else { "FAIL  $name"; $script:fail++ } }
Expect 'field mode loaded from the ini'             (($log | Select-String 'settings loaded').Count -ge 1)
Expect 'passes kept as recorded (field mode line)'  (($log | Select-String 'field mode: pass of \d+ frames kept as recorded').Count -ge 3)
Expect 'no surface optimisation'                    (($log | Select-String 'surface optimisation \(fine registration').Count -eq 0)
Expect 'no pass refinement'                         (($log | Select-String 'pass refinement').Count -eq 0)
Expect 'no auto-align after Stop'                   (($log | Select-String 'auto-align queued|auto-align 0x').Count -eq 0)
Expect 'live search still joins'                    (($log | Select-String '-> FOUND').Count -ge 1)
# autosave cannot be checked here: a replay never autosaves (rec_sdk.h autosaveScan). The 'fresh' flag that triggers it
# is the same in field mode; tests/system sys_autosave_* cover the autosave itself.
Expect 'script checks all ok'                       ((($log | Select-String 'script ok').Count -ge 5) -and (($log | Select-String 'script FAIL').Count -eq 0))
Copy-Item "$bin\artecrec.log" "D:\ArtecReplay\results\log-field-mode.txt" -Force -ErrorAction SilentlyContinue
"field-mode-check: $fail failure(s)"; exit $fail
