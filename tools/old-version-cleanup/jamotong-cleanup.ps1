# jamotong-cleanup.ps1 - removes what older Jamotong versions left on this PC (0.70.0, owner request 2026-10-04).
#
#   - Installed products: the separate language pack MSIs of 0.62-0.69 ("Jamotong Chinese ...") and a Jamotong
#     installer older than 0.70.0. Jamotong 0.70.0 and later is kept.
#   - The old zip packs in your profile (%APPDATA%\Jamotong): the Japanese pack of 0.44-0.49 and the Chinese pack of
#     0.48-0.61 (layout and dictionaries). Recognised by their layout file; moved to a backup folder, not deleted.
#   - Language data an old pack left in the install folder that no installed product owns any more.
#
# It lists what it found and asks before it changes anything. Usage (cleanup.cmd runs this):
#   jamotong-cleanup.ps1            list, ask, remove
#   jamotong-cleanup.ps1 -List      only list
#   jamotong-cleanup.ps1 -Yes       do not ask
param([switch]$List, [switch]$Yes, [string]$UserAppData = $env:APPDATA)

$ErrorActionPreference = 'Continue'
$Keep = [version]'0.70.0'
$log = Join-Path $env:TEMP 'jamotong-cleanup.log'
function Say($t) { Write-Host $t; Add-Content -Path $log -Value $t -Encoding UTF8 }

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin -and -not $List) {
    # Removing products needs administrator rights. The profile to clean is this user's, so pass it on.
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-UserAppData', "`"$UserAppData`"")
    if ($Yes) { $a += '-Yes' }
    try { Start-Process powershell -Verb RunAs -ArgumentList $a | Out-Null } catch { Write-Host 'Administrator rights were refused - nothing was changed.' }
    exit
}

Say ("== Jamotong cleanup " + (Get-Date -Format 's'))

# 1. Installed products
$products = @()
foreach ($root in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall') {
    Get-ChildItem $root -ErrorAction SilentlyContinue | ForEach-Object {
        $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
        if (-not $p.DisplayName -or $p.DisplayName -notlike 'Jamotong*' -or $p.Publisher -ne 'Jamotong') { return }
        if ($_.PSChildName -notmatch '^\{[0-9A-Fa-f-]{36}\}$') { return }
        $pack = $p.DisplayName -ne 'Jamotong'
        $old = $pack
        if (-not $pack) { try { $old = [version]$p.DisplayVersion -lt $Keep } catch { $old = $false } }
        if ($old) { $products += [pscustomobject]@{ Name = $p.DisplayName; Version = $p.DisplayVersion; Code = $_.PSChildName; Pack = $pack } }
    }
}

# 2. Old zip packs in the profile: a layout file is the pack's when its header matches (a layout of your own with
#    another name or content is never touched).
$userItems = @()
$lay = Join-Path $UserAppData 'Jamotong\layouts'
$dic = Join-Path $UserAppData 'Jamotong\dicts'
function OldPack($jmt, $name, $mustLack, $dicts, $stem) {
    $f = Join-Path $lay $jmt
    if (-not (Test-Path $f)) { return }
    $t = Get-Content $f -Raw -Encoding UTF8
    if ($t -notmatch "(?m)^Name\s*=\s*$([regex]::Escape($name))\s*$" -or ($mustLack -and $t -match $mustLack)) { return }
    $script:userItems += $f
    Get-ChildItem $lay -Filter "$stem*.jmb" -ErrorAction SilentlyContinue | ForEach-Object { $script:userItems += $_.FullName }
    foreach ($d in $dicts) { $p = Join-Path $dic $d; if (Test-Path $p) { $script:userItems += $p } }
}
OldPack 'japanese.jmt' 'Japanese (romaji)' '(?m)^Japanese\s*=' @('japanese.jdb', 'romaji-kana.jdb') 'japanese'
OldPack 'chinese.jmt' 'Chinese (pinyin)' $null @('chinese.jdb', 'pinyin-keys.jdb') 'chinese'

# 3. Data an old pack left in the install folder (0.62's combined pack used these names; 0.70.0 does not)
$machineItems = @()
$dir = (Get-ItemProperty 'HKLM:\SOFTWARE\Jamotong' -Name InstallDir -ErrorAction SilentlyContinue).InstallDir
if (-not $dir) { $dir = Join-Path $env:ProgramFiles 'Jamotong' }
foreach ($n in 'chinese.jdb', 'pinyin-keys.jdb', 'chinese.jmt') {
    $p = Join-Path $dir $n
    if (Test-Path $p) { $machineItems += $p }
}
Get-ChildItem $dir -Filter 'chinese.v*.jmb' -ErrorAction SilentlyContinue | ForEach-Object { $machineItems += $_.FullName }

if (-not $products -and -not $userItems -and -not $machineItems) {
    Say 'Nothing from older versions was found. / 예전 판이 남긴 것이 없습니다.'
    if (-not $Yes) { Read-Host 'Enter to close' | Out-Null }
    exit 0
}
Say 'Found / 찾은 것:'
foreach ($p in $products) { Say ("  installed: " + $p.Name + " " + $p.Version) }
foreach ($f in $userItems) { Say ("  old pack file: " + $f) }
foreach ($f in $machineItems) { Say ("  left-over data: " + $f) }
if ($List) { exit 0 }
if (-not $Yes) {
    $a = Read-Host 'Remove them? / 지울까요? (y/N)'
    if ($a -notmatch '^[yY]') { Say 'Nothing was changed.'; exit 0 }
}

# Packs first, then an old Jamotong. Programs that are open keep their files until they close; no restart is forced.
foreach ($p in ($products | Sort-Object { -not $_.Pack })) {
    $r = Start-Process msiexec -ArgumentList '/x', $p.Code, '/qn', '/norestart' -Wait -PassThru
    $ok = $r.ExitCode -in 0, 1605, 3010
    Say ("  removed " + $p.Name + " " + $p.Version + " -> " + $r.ExitCode + $(if ($ok) { '' } else { ' (failed)' }))
}
if ($userItems) {
    $bak = Join-Path $UserAppData ('Jamotong\old-pack-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force $bak | Out-Null
    foreach ($f in $userItems) { Move-Item $f $bak -Force -ErrorAction SilentlyContinue; Say ("  moved " + $f + " -> " + $bak) }
}
foreach ($f in $machineItems) {
    Remove-Item $f -Force -ErrorAction SilentlyContinue
    if (Test-Path $f) { Say ("  in use, left for now: " + $f) } else { Say ("  deleted " + $f) }
}
if ($products | Where-Object { -not $_.Pack }) {
    Say 'Jamotong itself was an old version and has been removed. Install the latest from https://github.com/rubidus-api/jamotong_ime/releases/latest'
}
Say ("Done. Log: " + $log)
if (-not $Yes) { Read-Host 'Enter to close' | Out-Null }
