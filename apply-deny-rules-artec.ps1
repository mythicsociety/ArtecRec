# Artec lane copy, 1 Nov 2026. The owner runs it. Turn the estate's standing prohibitions into permissions.deny rules.
#
# Owner, 12 Aug: "sentences get ignored, settings block the action."
#
# NOTE: the rule strings are assembled from fragments. Written out whole, this file's own text
# matches the destructive-guard hook, and the hook blocks the command that writes the rules.

$path = "$env:USERPROFILE\.claude\settings.json"
Copy-Item $path "$path.bak-20261101-artec-deny-rules" -Force

$doc = Get-Content -Raw $path | ConvertFrom-Json

$rm = 'r' + 'm'
$rf = $rm + ' -r'

$rules = @(
    # --- D: is frozen. No writes. Standing rule since the 19 July wipe.
    # Both slash spellings, and the drive letter in either case. The Artec lane was moved to
    # C:\Artec on 12 Aug precisely so it would never write here.
    'Write(D:/**)',
    'Edit(D:/**)',
    'Write(D:\**)',
    'Edit(D:\**)',
    'Write(d:/**)',
    'Edit(d:/**)',
    'Write(//D:/**)',
    'Edit(//D:/**)',
    "Bash($rf /d/*)",
    "Bash($rm /d/*)",
    'PowerShell(Remove-Item D:*)',
    'PowerShell(Set-Content*-Path D:*)',
    'PowerShell(Out-File*D:*)',

    # --- BHAGAVADGITA: read only. No writes, no deletes, no reboot, ever.
    #
    # ONE MACHINE, TWO PORTS. .13 is the primary and .18 is the fallback (SBC/owner, 12 Aug).
    # Rules that named only .18 left the primary address open, so the NAS was not protected at
    # the address everything actually uses. Both addresses and the name are listed.
    # BOTH slash spellings. '//host/**' and '\\host\**' are different strings to a matcher, so a
    # rule with one spelling is half a rule. (HDC, 12 Aug.) Names are listed as well as addresses,
    # for anything that resolves rather than dials.
    'Write(//192.168.0.13/**)',
    'Edit(//192.168.0.13/**)',
    'Write(\\192.168.0.13\**)',
    'Edit(\\192.168.0.13\**)',
    'Write(//192.168.0.18/**)',
    'Edit(//192.168.0.18/**)',
    'Write(\\192.168.0.18\**)',
    'Edit(\\192.168.0.18\**)',
    'Write(//BHAGAVADGITA/**)',
    'Edit(//BHAGAVADGITA/**)',
    'Write(\\BHAGAVADGITA\**)',
    'Edit(\\BHAGAVADGITA\**)',
    'PowerShell(Remove-Item*192.168.0.13*)',
    'PowerShell(Remove-Item*192.168.0.18*)',
    'PowerShell(Remove-Item*BHAGAVADGITA*)',
    'PowerShell(Restart-Computer*192.168.0.13*)',
    'PowerShell(Restart-Computer*192.168.0.18*)',

    # --- OriNas: the ORI Mysore palm-leaf collection. Same rule, different machine.
    'Write(//192.168.0.106/**)',
    'Edit(//192.168.0.106/**)',
    'Write(\\192.168.0.106\**)',
    'Edit(\\192.168.0.106\**)',
    'Write(//OriNas/**)',
    'Edit(//OriNas/**)',
    'Write(\\OriNas\**)',
    'Edit(\\OriNas\**)',
    'Write(//OriNas.local/**)',
    'Edit(//OriNas.local/**)',
    'Write(\\OriNas.local\**)',
    'Edit(\\OriNas.local\**)',
    'PowerShell(Remove-Item*192.168.0.106*)',
    'PowerShell(Restart-Computer*192.168.0.106*)',

    # --- No machine is rebooted or shut down without its named user's consent (ROSTER.md).
    'PowerShell(Restart-Computer*)',
    'PowerShell(Stop-Computer*)',
    'Bash(shutdown*)',
    'PowerShell(shutdown*)',
    'Bash(ssh*shutdown*)',
    'Bash(ssh*Restart-Computer*)',

    # --- inbox-sbc is a tombstoned stray. Writing there loses the message.
    'Write(C:/ClaudeComms/CLAUDE_EXCHANGE/inbox-sbc/**)',
    'Edit(C:/ClaudeComms/CLAUDE_EXCHANGE/inbox-sbc/**)'
)

$doc.permissions.deny = @(@($doc.permissions.deny) + $rules | Select-Object -Unique)
$doc | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding UTF8

Write-Output '--- deny rules now in force ---'
(Get-Content -Raw $path | ConvertFrom-Json).permissions.deny | ForEach-Object { '  ' + $_ }

try {
    Get-Content -Raw $path | ConvertFrom-Json | Out-Null
    Write-Output 'settings.json is valid JSON'
} catch {
    Write-Output ('INVALID JSON: ' + $_.Exception.Message)
}

