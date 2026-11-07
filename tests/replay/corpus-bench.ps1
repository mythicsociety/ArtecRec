# Overnight replay benchmark over the copied corpus. First the scripted field-bug test on Trail14, then every corpus
# project in "mid" mode (a resume over already-scanned surface), then "start" mode if time remains.
$env:PATH = "C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64;" + $env:PATH
$bin = 'D:\ArtecReplay\bin'; $res = 'D:\ArtecReplay\results'; New-Item -ItemType Directory -Force $bin, $res | Out-Null
Copy-Item C:\Artec\sdk-fps\artecrec.exe $bin -Force; if (Test-Path C:\Artec\sdk-fps\artecrec.exe.manifest) { Copy-Item C:\Artec\sdk-fps\artecrec.exe.manifest $bin -Force }
$log = "$res\driver.log"; $t0 = Get-Date
function Run($cmdline, $tag, $maxMin) {   # not $args: that is PowerShell's own automatic variable (it came out empty)
  $ts = Get-Date; $p = Start-Process "$bin\artecrec.exe" -ArgumentList $cmdline -WorkingDirectory $bin -PassThru
  $p.WaitForExit($maxMin * 60000) | Out-Null; $killed = $false; if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $killed = $true }
  "$(Get-Date -Format 'HH:mm') $tag : $([int]((Get-Date)-$ts).TotalSeconds) s$(if ($killed) { ' KILLED (timeout)' })" | Add-Content $log
  Copy-Item "$bin\artecrec.log" "$res\log-$tag.txt" -Force -ErrorAction SilentlyContinue
}
Run "--replay-script C:\Artec\tests\e2e\replay-field-bugs.txt $env:TEMP\artecrec-tests\Trail14\Trail14.sproj" 'script-trail14' 10
$projects = Get-ChildItem 'D:\ArtecReplay\corpus' -Directory | ForEach-Object { Get-ChildItem $_.FullName -Filter *.sproj -Recurse -Depth 2 | Select-Object -First 1 } | Sort-Object Length
"$(Get-Date -Format 'HH:mm') $($projects.Count) project(s)" | Add-Content $log
foreach ($mode in @('mid', 'start')) {
  foreach ($pr in $projects) {
    $tag = "$mode-" + ((Split-Path (Split-Path $pr.FullName -Parent) -Leaf) -replace '[^\w-]', '_')
    $extra = if ($mode -eq 'mid') { ' --replay-from-mid' } else { '' }
    Run "--replay-bench $res\bench-$mode.csv$extra `"$($pr.FullName)`"" $tag 25
  }
  "$(Get-Date -Format 'HH:mm') mode $mode done, elapsed $([int]((Get-Date)-$t0).TotalMinutes) min" | Add-Content $log
}
"DONE $(Get-Date -Format 'HH:mm'), $([int]((Get-Date)-$t0).TotalMinutes) min" | Add-Content $log
