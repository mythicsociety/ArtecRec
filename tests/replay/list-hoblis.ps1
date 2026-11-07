# READ-ONLY: lists the Studio projects in a few hobli folders of FinishedModels with scans, frames, sizes and exports.
$root = '\\192.168.0.18\AllProjectData\3D_data\FinishedModels'
$folders = @('Bangalore\YELAHANKA HOBLI', 'Nelamangala\Mailanahalli Hobli', 'Anekal\Attibele_Hobli', 'Magadi\Satanuru Hobli', 'Hoskote\KADUGODI HOBLI', '41 Undocumented Bengaluru Inscriptions')
$out = "$PSScriptRoot\finished-projects.csv"; "project,scans,maxFrames,scanFiles,scansMB,export,exportMB" | Set-Content $out -Encoding UTF8
foreach ($f in $folders) {
  Get-ChildItem (Join-Path $root $f) -Filter *.sproj -Recurse -Depth 4 -File -ErrorAction SilentlyContinue | ForEach-Object {
    try { $x = Get-Content $_.FullName -Raw -ErrorAction Stop; $scans = ([regex]::Matches($x, '<Scan ')).Count
      $fr = [regex]::Matches($x, 'Items=.(\d+)') | ForEach-Object { [int]$_.Groups[1].Value } | Measure-Object -Maximum | Select-Object -ExpandProperty Maximum
      $dir = $_.DirectoryName; $files = @(Get-ChildItem "$dir\Scans" -File -Recurse -ErrorAction SilentlyContinue); $mb = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB)
      $ex = Get-ChildItem "$dir\Exports" -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.Extension -match '^\.(stl|ply)$' } | Sort-Object Length | Select-Object -First 1
      "$($_.FullName.Substring($root.Length + 1).Replace(',', ';')),$scans,$fr,$($files.Count),$mb,$(if ($ex) { $ex.Name.Replace(',', ';') } else { '' }),$(if ($ex) { [math]::Round($ex.Length/1MB) } else { 0 })" | Add-Content $out -Encoding UTF8
    } catch { }
  }
  "done $f $(Get-Date -Format HH:mm)" | Add-Content "$PSScriptRoot\list-hoblis.log"
}
"DONE" | Add-Content "$PSScriptRoot\list-hoblis.log"
