# Tracking loss keeps the scan open (7 Nov, museum day). Replays tests\e2e\replay-loss-patient.txt and checks the log.
# Usage: loss-check.ps1 [-Folder <finished project>] [-Exe C:\Artec\sdk-fps\artecrec.exe]
param([string]$Folder = (Get-ChildItem 'D:\ArtecReplay\finished' -Directory -Filter '*Maranay*' | Select-Object -First 1).FullName,
      [string]$Exe = 'C:\Artec\sdk-fps\artecrec.exe', [string]$Script = 'C:\Artec\tests\e2e\replay-loss-patient.txt', [int]$MaxMin = 15)
$env:PATH = "C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64;" + $env:PATH
$bin = 'D:\ArtecReplay\bin-loss'; New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item $Exe $bin -Force; Copy-Item ($Exe + '.manifest') $bin -Force -ErrorAction SilentlyContinue
Remove-Item "$bin\artecrec.ini", "$bin\artecrec.log" -ErrorAction SilentlyContinue
$pr = Get-ChildItem $Folder -Filter *.sproj -File | Select-Object -First 1; if (-not $pr) { "no .sproj in $Folder"; exit 2 }
$p = Start-Process "$bin\artecrec.exe" -ArgumentList "--replay-script `"$Script`" `"$($pr.FullName)`"" -WorkingDirectory $bin -PassThru
$p.WaitForExit($MaxMin * 60000) | Out-Null; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; "KILLED (timeout)" }
$log = Get-Content "$bin\artecrec.log"
$fail = 0
function Expect($name, $ok) { if ($ok) { "PASS  $name" } else { "FAIL  $name"; $script:fail++ } }
Expect 'loss logged as "scan stays open"'      (($log | Select-String 'tracking lost: the scan stays open').Count -ge 2)
Expect 'tracking came back in the same scan'   (($log | Select-String 'tracking back after .* same scan continues').Count -ge 2)
Expect 'no scan ended by a loss'               (($log | Select-String 'ending this scan').Count -eq 0)
Expect 'no stub discarded'                     (($log | Select-String 'discarded').Count -eq 0)
Expect 'script checks all ok'                  ((($log | Select-String 'script ok').Count -ge 4) -and (($log | Select-String 'script FAIL').Count -eq 0))
Copy-Item "$bin\artecrec.log" "D:\ArtecReplay\results\log-loss-patient.txt" -Force -ErrorAction SilentlyContinue
"loss-check: $fail failure(s)"; exit $fail
