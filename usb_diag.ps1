$out = Join-Path $PSScriptRoot 'logs\usb_diag.txt'; New-Item -ItemType Directory -Force (Join-Path $PSScriptRoot 'logs') | Out-Null
$sb = {
"=== Get-Disk ==="; Get-Disk | Format-List Number,FriendlyName,BusType,Size,PartitionStyle,OperationalStatus,HealthStatus,IsReadOnly,IsOffline
"=== Get-PhysicalDisk ==="; Get-PhysicalDisk | Format-List FriendlyName,BusType,Size,OperationalStatus,HealthStatus
"=== Get-Partition (disks with USB bus) ==="; Get-Disk | Where-Object BusType -eq 'USB' | ForEach-Object { Get-Partition -DiskNumber $_.Number -ErrorAction SilentlyContinue | Format-List DiskNumber,PartitionNumber,DriveLetter,Size,Type }
"=== Get-Volume ==="; Get-Volume | Format-Table DriveLetter,FileSystem,DriveType,HealthStatus,Size -AutoSize
"=== PnP USB / DiskDrive devices ==="; Get-PnpDevice -Class USB,DiskDrive -ErrorAction SilentlyContinue | Format-Table Status,Class,FriendlyName,Problem -AutoSize
"=== Recent disk/USB errors (System log, 2h) ==="; Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddHours(-2)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'disk|usb|Ntfs|partmgr|volmgr|VDS|Kernel-PnP' -and $_.Level -le 3 } | Select-Object -First 25 TimeCreated,ProviderName,Id,Message | Format-List
}
& $sb *>&1 | Out-String -Width 200 | Set-Content -Path $out -Encoding UTF8
