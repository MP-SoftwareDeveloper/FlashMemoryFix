# Fixing two 64 GB USB sticks to FAT32

Both 64 GB sticks now hold an 8 GB FAT32 partition with the copied file (19,428,478 bytes) in the root.
Windows offers no FAT32 for disks over 32 GB, so a diskpart script made the partition instead.

| Stick | What went wrong | What worked | Result |
| --- | --- | --- | --- |
| Kingston DataTraveler 3.0 (57.7 GiB) | `clean` refused: access denied, "cannot zero sectors" (error 5). Later Windows showed it as 0 B, Unusable. | Plug it in alone, then create the partition with a script that skips `clean`. | OK. File size and MD5 match the original. |
| Generic "Mass Storage Device" (62.5 GiB) | `clean` worked, then "No usable free extent"; `convert mbr` said "not convertible". | Same script, with `attributes disk clear readonly` first, also no `clean`. | OK. File size matched in the log; MD5 not checked. |

## What went wrong

Windows' Format dialog only lists FAT or exFAT for sticks over 32 GB, but some devices need FAT32.
The usual workaround is a small FAT32 partition made with diskpart, and that failed in two different ways.

- **Generic stick:** `clean` succeeded, but `create partition primary` said "No usable free extent".
  `list partition` showed one 62 GB "partition" at offset 0 that could not be selected, and `convert mbr`
  failed with "The specified disk is not convertible".
- **Kingston stick:** `clean` failed with "Access is denied". The Windows System log recorded
  "Cannot zero sectors on disk PhysicalDrive2. Error code: 5".
- **Both, afterwards:** with both plugged in, `list volume` showed F: and G: as Removable, 0 B, Unusable,
  and the Format dialog said "Unknown capacity" and "Windows was unable to complete the format".

## What the diagnostic showed

`usb_diag.ps1` (read-only) showed neither stick was dead. Both were Online, Healthy, MBR, not read-only and had
no partitions at all, so Windows had nothing to mount. That explains the 0 B volumes.

| Stick | Bus | Size (bytes) | Partition style | Health |
| --- | --- | --- | --- | --- |
| Kingston DataTraveler 3.0 | USB | 61,920,509,952 | MBR | Healthy |
| Mass Storage Device | USB | 67,109,912,576 | MBR | Healthy |

The only logged error was the "cannot zero sectors" entry from the Kingston's failed `clean`. I could not tell
from this data why it happened. I earlier blamed a read-only lock on the generic stick. That was a mix-up: the
error belonged to the Kingston, and `attributes disk` showed Read-only: No.

## The fix

One script per stick, each run by you as administrator because formatting erases a disk. Each script:

1. Finds the target by bus type (USB), name and size, and stops unless it finds exactly one match,
   so it cannot touch your PC drives.
2. Shows the disk number and asks you to type `YES`.
3. Makes one 8 GB primary partition, formats it as FAT32 (quick), and assigns a drive letter.
   It skips `clean` when the disk is already empty.
4. Copies the first file in `payload\` to the root of the stick and compares the byte count with the source.
5. Writes a log to `logs\`.

## Files in this folder

| File | What it does |
| --- | --- |
| `make_fat32.bat` / `.ps1` | Targets the **Kingston** stick (USB, name contains "Kingston", 50 to 70 GB). |
| `make_fat32_generic.bat` / `.ps1` | Targets the generic "Mass Storage Device" stick (60 to 70 GB) and runs `attributes disk clear readonly` first. |
| `usb_diag.bat` / `.ps1` | Read-only: lists disks, partitions, volumes, USB devices and recent disk errors. |
| `payload\` | The file that gets copied onto the stick (the first file in the folder). |
| `logs\` | The logs from the runs described above. |

**How to use:** plug in only the target stick. Right-click the `make_fat32*.bat` file and choose
**Run as administrator**. Check the disk line, type `YES`, and read the `RESULT:` line at the end.
`usb_diag.bat` can be double-clicked normally.

The scripts and paths in this folder were adapted after the original runs (log and payload locations changed)
and have not been run from here yet. To target a different stick, change the `-like '*Kingston*'` name filter
and the size range near the top of the script.

## Code (abridged from `make_fat32.ps1`)

```powershell
# 1. Find exactly one target stick, and refuse anything else
$cands = @(Get-Disk | Where-Object {
  $_.BusType -eq 'USB' -and $_.FriendlyName -like '*Kingston*' -and
  $_.Size -gt 50GB -and $_.Size -lt 70GB })
if ($cands.Count -ne 1) { throw "Expected exactly one Kingston USB disk" }
$d = $cands[0]

# 2. Ask before erasing
$ans = Read-Host "This ERASES Disk $($d.Number). Type YES to continue"
if ($ans -ne 'YES') { throw "Cancelled by user." }

# 3. diskpart script: 'clean' only if the disk already has partitions
$n = $d.Number
$script = @("select disk $n")
if (@(Get-Partition -DiskNumber $n -ErrorAction SilentlyContinue).Count -gt 0 -or
    $d.PartitionStyle -eq 'RAW') { $script += "clean" }
$script += "create partition primary size=8000"
$script += "format fs=fat32 quick label=GDSFW"
$script += "assign"
$script | Set-Content (Join-Path $env:TEMP 'dp.txt') -Encoding ASCII
diskpart /s (Join-Path $env:TEMP 'dp.txt')

# 4. Copy the file and compare sizes
$vol = Get-Partition -DiskNumber $n | Get-Volume |
       Where-Object FileSystem -eq 'FAT32' | Select-Object -First 1
Copy-Item $fw (Join-Path "$($vol.DriveLetter):\" (Split-Path $fw -Leaf)) -Force
```

## Results and verification

Both logs end with `RESULT: OK`. Each stick reads as FAT32 and holds only the copied file plus Windows'
hidden `System Volume Information` folder.

| Check | Kingston (F:) | Generic (G:) |
| --- | --- | --- |
| Copied size equals source (19,428,478 bytes) | Yes | Yes |
| Root listing read back from the stick by me | Yes | No |
| MD5 equals the original (`cbcc7967...c1a`) | Yes | Not checked |

What is not confirmed: why each stick failed before, and which single step fixed it. The most likely factors
are the stale 0 B state left by the earlier failed attempts, a clean re-plug of one stick at a time, and
skipping `clean`.

## FlashMemoryFix app (Qt 6 / C++)

A GUI that does what the scripts above do by hand: it lists the USB sticks and SD cards on the PC, explains
what is wrong with each, and offers **Clear read-only** and one combined **Clean + Partition + Format** action
(FAT32, exFAT or NTFS; MBR or GPT; label, size and allocation unit selectable). Cleaning is automatic: old partitions
are erased when there are any, and skipped on an empty disk (some sticks refuse `clean`). Optionally the app then
waits for the new drive, copies a file onto it and verifies the copy with SHA-256.

- Devices are read with the Win32 API (`\\.\PhysicalDriveN` + IOCTLs). Cleaning, partitioning and formatting use
  `diskpart /s`, the method that fixed both sticks above. The exact script is shown before it runs.
- Safety: only USB / SD / MMC devices can be changed, never the disk holding Windows; the user must type the disk
  number to confirm; the device is re-checked (same model, size and serial) right before the script runs.
- `FlashMemoryFix.exe --dry-run` shows the scripts without executing anything.
- FAT32 is capped at 32,000 MB because Windows refuses larger FAT32 volumes. The app says so and suggests exFAT.

Build (Qt 6.2+, CMake 3.21+):

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt install>\<version>\<kit>
cmake --build build
ctest --test-dir build
```

The exe asks for administrator rights at start. To launch it from Qt Creator without running Creator elevated,
configure with `-DFLASHFIX_REQUIRE_ADMIN=OFF` (the app then starts, but Clean / Format stay disabled).

Layout: `src/core` (data model, diskpart script builder, Win32 enumeration, no Qt), `src/ui` (Qt widgets),
`tests/test_core.cpp`. On non-Windows systems the app starts with fake demo disks.
