# samples Artec Studio on THIS PC every 5 s -> C:\Artec\sdk-fps\studiosample-desktop.csv
$out='C:\Artec\sdk-fps\studiosample-desktop.csv'
'time,astudio_cores,hot_thread_cores,threads_over_10pct,ws_mb,private_mb' | Set-Content $out
while($true){
  $p=Get-Process astudio_pro -EA SilentlyContinue | select -First 1
  if($p){ $t0=$p.TotalProcessorTime.TotalMilliseconds; $th0=@{}; $p.Threads | % { $th0[$_.Id]=$_.TotalProcessorTime.TotalMilliseconds }
    Start-Sleep 5
    $p=Get-Process astudio_pro -EA SilentlyContinue | select -First 1; if(-not $p){ continue }
    $cores=($p.TotalProcessorTime.TotalMilliseconds-$t0)/5000
    $d=$p.Threads | % { $o=$th0[$_.Id]; if($o -ne $null){ ($_.TotalProcessorTime.TotalMilliseconds-$o)/5000 } }
    $hot=($d | measure -Max).Maximum; $n10=@($d | ? { $_ -gt 0.1 }).Count
    Add-Content $out ("{0},{1:N2},{2:N2},{3},{4},{5}" -f (Get-Date -Format 'HH:mm:ss'),$cores,$hot,$n10,[int]($p.WorkingSet64/1MB),[int]($p.PrivateMemorySize64/1MB))
  } else { Start-Sleep 5 } }
