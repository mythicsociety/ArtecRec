<#
verify-log.ps1 - check an artecrec.log from a field-test session (see FIELD-TEST.md).

  powershell -ExecutionPolicy Bypass -File verify-log.ps1 -Log D:\ArtecRec\artecrec.log
  powershell -ExecutionPolicy Bypass -File verify-log.ps1 -Log artecrec.log -Since "10:30:00"    # only lines from this time on (same day)
  powershell -ExecutionPolicy Bypass -File verify-log.ps1 -Log artecrec.log -Steps record,save   # only some expected steps

Prints PASS / FAIL / WARN / SKIP per check and exits 1 if any check FAILED.
Log line format (rec_sdk.h logf): "HH:MM:SS  message". The file is appended to by every run of ArtecRec; a run
starts with "... by Mythic Society start" and a clean run ends with "ArtecRec exit".
The message patterns are in $P below: if the app's log wording changes, change them there (one place).
#>
param(
  [Parameter(Mandatory = $true)][string]$Log,
  [string]$Since = "",
  [string[]]$Steps = @("record", "trackingloss", "usbpull", "save", "reopen"),
  [int]$MaxGapSec = 65,        # the app writes "rec fps ..." every 60 s while recording; more silence = UI thread stuck
  [double]$MinFps = 6.0,       # below this the scanner is not keeping up (USB packet problem, Nov 2026)
  [double]$MaxMemGB = 40.0,    # working set warning (review E1)
  [switch]$Quiet
)
$ErrorActionPreference = "Stop"
if ($Steps.Count -eq 1 -and $Steps[0] -match ",") { $Steps = $Steps[0] -split "," }   # -Steps a,b from cmd.exe arrives as one string

# ---------------------------------------------------------------- log patterns (match rec_sdk.h / artecrec.cpp logf calls)
$P = @{
  start         = 'by Mythic Society start$'
  exit          = '^ArtecRec exit$'
  connected     = '^scanner \S+ connected, fps'
  noScanner     = '^no scanner at start'
  procStarted   = '^procedure started \((record|preview)'
  procStartRec  = '^procedure started \(record'
  procFinished  = '^procedure finished 0x([0-9A-F]{8}), scans added (\d+)'
  recFps        = '^rec fps ([\d.]+) frames (\d+) regFail (\d+) lost (\d+) memGB ([\d.]+)'
  trackLoss     = '^(tracking lost: ending this scan|no tracking for 3 s on a fresh recording)'
  disconnected  = '^scanner disconnected'
  reconnected   = '^scanner reconnected'
  saveLine      = '^save 0x([0-9A-F]{8}) ([\d.]+) s -> (.*)$'
  openLine      = '^open 0x([0-9A-F]{8}) ([\d.]+) s, scans (\d+)'
  optimised     = '^surface optimisation \(fine registration'
}
# failure signatures: any of these is a FAIL
$FAILSIG = [ordered]@{
  "Stop did not finish (stuck Stop, B3)"                = '^Stop did not finish'
  "a time limit fired: no result in N s (B1/B2)"        = 'no result in \d+ s'
  "could not replace the project folder (A1/A2)"        = '^could not replace the project folder'
  "auto-align skipped, scan still processing (B2)"      = '^auto-align skipped'
  "scanner procedure could not be created"              = '^createScanningProcedure 0x'
  "project could not be created/saved"                  = '^(createNewProject|createSaver) 0x'
  "project could not be opened"                         = '^openProject 0x'
  "cameras wedged: need a power cycle"                  = 'the cameras need a power cycle'
}
# warnings: worth a look, not a failure by themselves
$WARNSIG = [ordered]@{
  "surface optimisation failed (scan kept as recorded)" = '^surface optimisation failed'
  "global registration failed (scan kept)"              = '^global registration failed'
  "no frames from the scanner, capture restarted"       = '^no frames from the scanner in 6 s'
  "position not found during a scan"                    = '^position was not found during this scan'
  "scanner overheated"                                  = '^scanner overheated'
  "SDK error/warning lines"                             = '^\[sdk\]'
  "pass refinement found no overlap"                    = '^pass refinement: no overlap'
}

# ---------------------------------------------------------------- read and split
if (-not (Test-Path $Log)) { Write-Host "FAIL  log_exists  no file $Log"; exit 1 }
$raw = Get-Content -LiteralPath $Log -Encoding Default
$lines = New-Object System.Collections.Generic.List[object]
$dayOffset = 0; $prev = -1; $sinceSec = -1
if ($Since) { $t = [TimeSpan]::Parse($Since); $sinceSec = [int]$t.TotalSeconds }
foreach ($r in $raw) {
  if ($r -match '^(\d\d):(\d\d):(\d\d)  (.*)$') {
    $s = [int]$matches[1] * 3600 + [int]$matches[2] * 60 + [int]$matches[3]
    if ($prev -ge 0 -and $s + 3600 -lt $prev) { $dayOffset += 86400 }   # past midnight
    $prev = $s; $abs = $s + $dayOffset
    if ($sinceSec -ge 0 -and $dayOffset -eq 0 -and $s -lt $sinceSec) { continue }
    $lines.Add([pscustomobject]@{ t = $abs; clock = $r.Substring(0, 8); msg = $matches[4] })
  }
}
$sessions = New-Object System.Collections.Generic.List[object]; $cur = $null
foreach ($l in $lines) { if ($l.msg -match $P.start) { $cur = New-Object System.Collections.Generic.List[object]; $sessions.Add($cur) }; if ($null -ne $cur) { $cur.Add($l) } }

$results = New-Object System.Collections.Generic.List[object]
function Report($verdict, $name, $detail) { $results.Add([pscustomobject]@{ v = $verdict; n = $name; d = $detail }) }
function Find($pat) { return ,@($lines | Where-Object { $_.msg -match $pat }) }   # the comma keeps a 1-element array an array (PS 5.1)
function Show($l) { if ($l) { "$($l.clock)  $($l.msg)" } else { "" } }

# ---------------------------------------------------------------- structure
if ($sessions.Count -eq 0) { Report FAIL "sessions_found" "no 'ArtecRec ... start' line in $($lines.Count) timed lines"; }
else { Report PASS "sessions_found" "$($sessions.Count) run(s) of ArtecRec, $($lines.Count) lines" }
$crashed = @(); for ($i = 0; $i -lt $sessions.Count; $i++) { $s = $sessions[$i]; if (-not ($s | Where-Object { $_.msg -match $P.exit })) { $crashed += "run $($i+1) started $($s[0].clock), last line $(Show $s[$s.Count-1])" } }
if ($sessions.Count -gt 0) { if ($crashed.Count) { Report FAIL "every_run_exited_cleanly" ("ended without 'ArtecRec exit' (crash, kill or still running): " + ($crashed -join "; ")) } else { Report PASS "every_run_exited_cleanly" "" } }
$conn = Find $P.connected
if ($conn.Count) { Report PASS "scanner_connected" (Show $conn[0]) } else { Report FAIL "scanner_connected" "no 'scanner ... connected' line" }
$unfinished = @(); $bad = @()
for ($i = 0; $i -lt $sessions.Count; $i++) { $s = $sessions[$i]; $st = @($s | Where-Object { $_.msg -match $P.procStarted }).Count; $fi = @($s | Where-Object { $_.msg -match $P.procFinished }).Count; if ($st -ne $fi) { $unfinished += "run $($i+1): $st started, $fi finished" } }
if ($unfinished.Count) { Report FAIL "every_scan_procedure_finished" ($unfinished -join "; ") } else { Report PASS "every_scan_procedure_finished" "" }
$finErr = @($lines | Where-Object { $_.msg -match $P.procFinished -and $matches[1] -ne "00000000" })
if ($finErr.Count) { Report WARN "procedures_ended_without_error" ("$($finErr.Count) ended with an error code (normal after a USB pull): " + (Show $finErr[0])) } else { Report PASS "procedures_ended_without_error" "" }

# ---------------------------------------------------------------- expected steps
function Step($name) { return $Steps -contains $name }
if (Step "record") { $added = @($lines | Where-Object { $_.msg -match $P.procFinished -and [int]$matches[2] -gt 0 }); if ($added.Count) { Report PASS "step_record_a_pass" "$($added.Count) recording(s) added scans" } else { Report FAIL "step_record_a_pass" "no 'procedure finished ..., scans added N' with N > 0" }
  $opt = Find $P.optimised; if ($opt.Count) { Report PASS "scans_tidied_after_stop" "$($opt.Count) surface optimisation(s)" } else { Report WARN "scans_tidied_after_stop" "no 'surface optimisation (fine registration' line" } } else { Report SKIP "step_record_a_pass" "not in -Steps" }
if (Step "trackingloss") { $tl = Find $P.trackLoss; if ($tl.Count) { Report PASS "step_lose_tracking" (Show $tl[0]) } else { Report FAIL "step_lose_tracking" "no 'tracking lost: ending this scan' / 'no tracking for 3 s' line (step not done, or loss not detected)" } } else { Report SKIP "step_lose_tracking" "not in -Steps" }
if (Step "usbpull") {
  $dis = Find $P.disconnected
  if (-not $dis.Count) { Report FAIL "step_usb_pull_detected" "no 'scanner disconnected' line (step not done, or the pull was not noticed)" }
  else { Report PASS "step_usb_pull_detected" (Show $dis[0]); $missing = @()
    foreach ($d in $dis) { $after = @($lines | Where-Object { $_.t -gt $d.t -and ($_.msg -match $P.reconnected -or $_.msg -match $P.connected) }); if (-not $after.Count) { $missing += (Show $d) } }
    if ($missing.Count) { Report FAIL "scanner_came_back_after_pull" ("no reconnect after: " + ($missing -join "; ")) } else { Report PASS "scanner_came_back_after_pull" "" } }
} else { Report SKIP "step_usb_pull_detected" "not in -Steps" }
$saves = @($lines | Where-Object { $_.msg -match $P.saveLine })
$saveBad = @($saves | Where-Object { $_.msg -match $P.saveLine -and $matches[1] -ne "00000000" })
if (Step "save") { $ok = @($saves | Where-Object { $_.msg -match $P.saveLine -and $matches[1] -eq "00000000" }); if ($ok.Count) { Report PASS "step_save_project" "$($ok.Count) save(s); last: $(Show $ok[-1])" } else { Report FAIL "step_save_project" "no 'save 0x00000000' line" } } else { Report SKIP "step_save_project" "not in -Steps" }
if ($saveBad.Count) { Report FAIL "no_failed_saves" (Show $saveBad[0]) } else { Report PASS "no_failed_saves" "" }
$opens = @($lines | Where-Object { $_.msg -match $P.openLine }); $openBad = @($opens | Where-Object { $_.msg -match $P.openLine -and $matches[1] -ne "00000000" })
if (Step "reopen") { if ($opens.Count -gt $openBad.Count) { Report PASS "step_reopen_project" (Show $opens[-1]) } else { Report FAIL "step_reopen_project" "no 'open 0x00000000' line" } } else { Report SKIP "step_reopen_project" "not in -Steps" }
if ($openBad.Count) { Report FAIL "no_failed_opens" (Show $openBad[0]) } else { Report PASS "no_failed_opens" "" }

# ---------------------------------------------------------------- failure and warning signatures
foreach ($k in $FAILSIG.Keys) { $hit = Find $FAILSIG[$k]; if ($hit.Count) { Report FAIL ("absent: " + $k) ("$($hit.Count)x, first: " + (Show $hit[0])) } else { Report PASS ("absent: " + $k) "" } }
foreach ($k in $WARNSIG.Keys) { $hit = Find $WARNSIG[$k]; if ($hit.Count) { Report WARN ("absent: " + $k) ("$($hit.Count)x, first: " + (Show $hit[0])) } }

# ---------------------------------------------------------------- gaps while recording, fps, memory
$gaps = @()
foreach ($s in $sessions) { for ($i = 0; $i -lt $s.Count - 1; $i++) { $m = $s[$i].msg
    if ($m -match $P.recFps -or $m -match $P.procStartRec) { $g = $s[$i + 1].t - $s[$i].t; if ($g -gt $MaxGapSec) { $gaps += "$g s after $(Show $s[$i])" } } }
  $last = $s[$s.Count - 1]; if (($last.msg -match $P.recFps -or $last.msg -match $P.procStartRec)) { $gaps += "log ENDS while recording: $(Show $last)" } }
if ($gaps.Count) { Report FAIL "no_silence_while_recording" ("$($gaps.Count) gap(s) > $MaxGapSec s: " + ($gaps[0..([Math]::Min(2, $gaps.Count - 1))] -join "; ")) } else { Report PASS "no_silence_while_recording" "" }
$fps = @($lines | Where-Object { $_.msg -match $P.recFps } | ForEach-Object { $null = $_.msg -match $P.recFps; [pscustomobject]@{ l = $_; fps = [double]$matches[1]; mem = [double]$matches[5] } })
if ($fps.Count) {
  $slow = @($fps | Where-Object { $_.fps -lt $MinFps }); $minF = ($fps | Measure-Object fps -Minimum).Minimum; $maxM = ($fps | Measure-Object mem -Maximum).Maximum
  if ($slow.Count) { Report WARN "recording_fps" "$($slow.Count) of $($fps.Count) samples below $MinFps fps (min $minF): $(Show $slow[0].l)" } else { Report PASS "recording_fps" "min $minF fps over $($fps.Count) samples" }
  if ($maxM -gt $MaxMemGB) { Report WARN "memory_use" "up to $maxM GB in use (E1)" } else { Report PASS "memory_use" "max $maxM GB" }
} else { Report SKIP "recording_fps" "no 'rec fps' lines (no recording longer than 60 s)" }

# ---------------------------------------------------------------- print
$nf = @($results | Where-Object v -eq FAIL).Count; $nw = @($results | Where-Object v -eq WARN).Count; $np = @($results | Where-Object v -eq PASS).Count; $ns = @($results | Where-Object v -eq SKIP).Count
if (-not $Quiet) { foreach ($r in $results) { $line = "{0,-4}  {1}" -f $r.v, $r.n; if ($r.d) { $line += "  - " + $r.d }; $c = @{ PASS = "Green"; FAIL = "Red"; WARN = "Yellow"; SKIP = "Gray" }[$r.v]; Write-Host $line -ForegroundColor $c } }
Write-Host ("verify-log: {0} passed, {1} failed, {2} warnings, {3} skipped  ({4})" -f $np, $nf, $nw, $ns, $Log)
if ($nf -gt 0) { exit 1 } else { exit 0 }
