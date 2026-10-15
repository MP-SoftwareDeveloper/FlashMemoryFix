$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'logs\make_fat32.txt'; New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'logs') | Out-Null
Start-Transcript -Path $log -Force | Out-Null
try {
  $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if (-not $isAdmin) { throw "Run this as administrator: right-click make_fat32.bat > Run as administrator." }
  $fw = (Get-ChildItem (Join-Path $PSScriptRoot 'payload') -File | Select-Object -First 1).FullName
  if (-not $fw -or -not (Test-Path $fw)) { throw "Put the file to copy into the payload folder first." }
  $cands = @(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and $_.FriendlyName -like '*Kingston*' -and $_.Size -gt 50GB -and $_.Size -lt 70GB })
  if ($cands.Count -ne 1) { throw "Expected exactly one Kingston USB disk (50-70 GB), found $($cands.Count). Unplug other sticks and retry." }
  $d = $cands[0]
  "Target: Disk $($d.Number)  $($d.FriendlyName)  $([math]::Round($d.Size/1GB,1)) GB  $($d.PartitionStyle)"
  $ans = Read-Host "This ERASES Disk $($d.Number) (Kingston stick). Type YES to continue"
  if ($ans -ne 'YES') { throw "Cancelled by user." }
  $n = $d.Number
  $parts = @(Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue)
  $script = @()
  $script += "select disk $n"
  if ($parts.Count -gt 0 -or $d.PartitionStyle -eq 'RAW') { $script += "clean" }
  $script += "create partition primary size=8000"
  $script += "format fs=fat32 quick label=GDSFW"
  $script += "assign"
  $tmp = Join-Path $env:TEMP 'make_fat32_dp.txt'
  $script | Set-Content -Path $tmp -Encoding ASCII
  "--- diskpart script ---"; $script
  "--- diskpart output ---"
  diskpart /s $tmp
  Start-Sleep 3
  $vol = Get-Partition -DiskNumber $n | Get-Volume | Where-Object { $_.FileSystem -eq 'FAT32' } | Select-Object -First 1
  if (-not $vol) { throw "FAT32 volume not found after diskpart. See output above." }
  $dst = "$($vol.DriveLetter):\"
  "FAT32 volume: $dst"
  Copy-Item $fw (Join-Path $dst (Split-Path $fw -Leaf)) -Force
  $a = (Get-Item $fw).Length; $b = (Get-Item (Join-Path $dst (Split-Path $fw -Leaf))).Length
  "Source bytes: $a  Copied bytes: $b"
  if ($a -ne $b) { throw "Size mismatch after copy." }
  "--- Root of $dst ---"; Get-ChildItem $dst -Force | Format-Table Name,Length
  "RESULT: OK"
} catch { "RESULT: FAILED - $($_.Exception.Message)" }
Stop-Transcript | Out-Null
