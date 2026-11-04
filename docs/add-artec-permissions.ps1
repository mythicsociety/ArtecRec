$f = "$env:USERPROFILE\.claude\settings.json"
Copy-Item $f "$f.bak-$(Get-Date -Format yyyyMMdd-HHmmss)"
$j = Get-Content $f -Raw | ConvertFrom-Json
$new = @(
  'Monitor',
  'Bash(K=*)', 'Bash(S=*)', 'Bash(H=*)',
  'Bash(ssh -i /c/Users/Uday/.ssh/id_ed25519_lab4 *)',
  'Bash(scp -O *)',
  'Bash(bash /c/Users/Uday/AppData/Local/Temp/claude/*)',
  'Bash(bash C:/ClaudeComms/CLAUDE_EXCHANGE/keepalive/*)',
  'Bash(bash "/c/ClaudeComms/CLAUDE_EXCHANGE/keepalive/*)',
  'Bash(until *)', 'Bash(sleep *)', 'Bash(date *)', 'Bash(cut *)', 'Bash(tr *)',
  'Bash("/c/Program Files/Tailscale/tailscale.exe" *)',
  'Bash(ping *)',
  'PowerShell(Set-Location *)', 'PowerShell(cmd /c *)',
  'PowerShell(Get-Item *)', 'PowerShell(Get-Date *)',
  'Write(//c/Artec/**)', 'Edit(//c/Artec/**)',
  'Write(//c/ClaudeComms/**)', 'Edit(//c/ClaudeComms/**)',
  'Write(//c/Users/Uday/.claude/**)', 'Edit(//c/Users/Uday/.claude/**)'
)
$allow = [System.Collections.ArrayList]@($j.permissions.allow)
$added = 0
foreach ($r in $new) { if ($allow -notcontains $r) { [void]$allow.Add($r); $added++ } }
$j.permissions.allow = $allow.ToArray()
$j | ConvertTo-Json -Depth 10 | Set-Content $f -Encoding UTF8
"added $added rules; total allow rules $($allow.Count)"
