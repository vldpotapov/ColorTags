<#
.SYNOPSIS
    Registers the ColorTags icon overlay handlers.

.DESCRIPTION
    An icon overlay handler lets Explorer draw the colored mark itself, as part
    of the item's icon. Nothing of ours is on screen, so there is no window to
    keep in the right z-order, no row to track, and no scrolling to keep up
    with - in any view mode, not only Details.

    The cost is a scarce resource. Windows reads
    HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers,
    sorts the entries by name and honours only the FIRST 15. Everything past
    that is silently ignored, which is how OneDrive and Dropbox users lose their
    sync badges to another program. "ColorTags ..." sorts before "OneDrive ...",
    so our entries win ties by alphabet alone - this script therefore reports
    exactly what the list looks like afterwards and refuses to push anyone out
    without -Force.

    Registration needs elevation and is applied with one reg.exe import, the
    same way register.ps1 does it. Explorer must be restarted to pick it up.

.EXAMPLE
    .\register_icon_overlays.ps1 -WhatIfSlots

.EXAMPLE
    .\register_icon_overlays.ps1 -Colors red -RestartExplorer

.EXAMPLE
    .\register_icon_overlays.ps1 -All -RestartExplorer
#>
[CmdletBinding()]
param(
    # Start with one color. A mechanism that has not been seen working should
    # not be holding seven of fifteen system-wide slots.
    [ValidateSet('red', 'orange', 'yellow', 'green', 'blue', 'purple', 'gray')]
    [string[]] $Colors = @('red'),

    [switch] $All,

    # Report what the slot list would look like and change nothing.
    [switch] $WhatIfSlots,

    # Proceed even when registering would push another program's overlay out of
    # the first 15.
    [switch] $Force,

    [switch] $RestartExplorer
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# Must match kOverlayClasses in tags_menu.cpp.
$overlayClasses = [ordered]@{
    red    = @{ Clsid = '{CC46952F-1C86-46B4-8C49-AA90F8DBDA56}'; Name = 'ColorTags Red' }
    orange = @{ Clsid = '{580D6C3D-7C45-4EEF-8C80-085B852B5C74}'; Name = 'ColorTags Orange' }
    yellow = @{ Clsid = '{D9D4BE22-2CF8-4F7E-B9E7-4F7500DD4365}'; Name = 'ColorTags Yellow' }
    green  = @{ Clsid = '{523F13B4-B99A-46B8-829A-FB9E0E6A6C0D}'; Name = 'ColorTags Green' }
    blue   = @{ Clsid = '{E8889757-2B0D-4C63-B867-9FC5C9AD946E}'; Name = 'ColorTags Blue' }
    purple = @{ Clsid = '{C9A70174-6FDB-4E4A-AFCA-1F3733325CBD}'; Name = 'ColorTags Purple' }
    gray   = @{ Clsid = '{3E8FF435-6341-43BD-A962-0DA454E1B9CD}'; Name = 'ColorTags Gray' }
}

if ($All) { $Colors = @($overlayClasses.Keys) }
$Colors = @($Colors | Select-Object -Unique)

$overlayKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers'
$slotLimit = 15

# ---------------------------------------------------------------- slot report
$existing = @()
if (Test-Path $overlayKey) {
    $existing = @(Get-ChildItem $overlayKey | ForEach-Object PSChildName)
}
$ours = @($Colors | ForEach-Object { $overlayClasses[$_].Name })
$combined = @($existing | Where-Object { $ours -notcontains $_ }) + $ours
$sorted = [string[]]$combined
[Array]::Sort($sorted, [StringComparer]::OrdinalIgnoreCase)

$report = @()
$report += "Overlay slots: $($existing.Count) registered now, $($sorted.Count) after this change, limit $slotLimit."
$report += ''
for ($i = 0; $i -lt $sorted.Count; $i++) {
    $mark = if ($i -lt $slotLimit) { '  ' } else { '!!' }
    $mine = if ($ours -contains $sorted[$i]) { ' <- ColorTags' } else { '' }
    $report += ("{0} {1,2}. {2}{3}" -f $mark, ($i + 1), $sorted[$i], $mine)
}
$report += ''

$droppedNow = @($sorted | Select-Object -Skip $slotLimit)
$existingSorted = [string[]]$existing
[Array]::Sort($existingSorted, [StringComparer]::OrdinalIgnoreCase)
$droppedBefore = @($existingSorted | Select-Object -Skip $slotLimit)
$newlyDropped = @($droppedNow | Where-Object { $droppedBefore -notcontains $_ -and $ours -notcontains $_ })
$oursDropped = @($droppedNow | Where-Object { $ours -contains $_ })

if ($oursDropped.Count) {
    $report += "WARNING: these ColorTags handlers fall past slot $slotLimit and will NOT be drawn: $($oursDropped -join ', ')"
}
if ($newlyDropped.Count) {
    $report += "WARNING: registering this would push another program's overlays out of the first ${slotLimit}: $($newlyDropped -join ', ')"
    $report += "WARNING: those badges (sync status and the like) would stop being drawn. Re-run with -Force to accept that."
}
if (-not $oursDropped.Count -and -not $newlyDropped.Count) {
    $report += "All requested handlers fit inside the first $slotLimit slots, and nothing else is displaced."
}

# The report also goes to a file: it is the one thing that has to survive being
# read by someone else, and a console buffer is easy to lose.
$reportPath = Join-Path $env:LOCALAPPDATA 'ColorTags\overlay-slots.txt'
try {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $reportPath) | Out-Null
    $report | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $report | ForEach-Object { Write-Output $_ }
    Write-Output "Report also written to: $reportPath"
} catch {
    $report | ForEach-Object { Write-Output $_ }
    Write-Warning "Could not write the report file: $($_.Exception.Message)"
}

if ($WhatIfSlots) { return }
if ($newlyDropped.Count -and -not $Force) {
    throw "Refusing to displace another program's overlays. Re-run with -Force, or register fewer colors."
}

# ------------------------------------------------------------------- install
# Explorer loads this outside any package context, so the handler must come
# from a plain user-writable path, exactly as the property handler does.
$builtDll = Join-Path $root 'out\ColorTagsMenu.dll'
if (-not (Test-Path -LiteralPath $builtDll)) {
    $builtDll = Join-Path $root 'ColorTagsMenu.dll'
}
if (-not (Test-Path -LiteralPath $builtDll)) { throw "Build the extension first: $builtDll is missing." }
$builtIcons = Join-Path $root 'out\icons'
if (-not (Test-Path -LiteralPath $builtIcons)) {
    $builtIcons = Join-Path $root 'icons'
}
if (-not (Test-Path -LiteralPath $builtIcons)) { throw "Build the extension first: $builtIcons is missing." }

$installDir = Join-Path $env:LOCALAPPDATA 'Colortags'
New-Item -ItemType Directory -Force -Path $installDir | Out-Null
$hash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
$installedDll = Join-Path $installDir "ColorTagsMenu-$hash.dll"
if (-not (Test-Path -LiteralPath $installedDll)) {
    Copy-Item -LiteralPath $builtDll -Destination $installedDll -Force
}
# The handler builds the icon path from its own module directory.
$installedIcons = Join-Path $installDir 'icons'
New-Item -ItemType Directory -Force -Path $installedIcons | Out-Null
Copy-Item (Join-Path $builtIcons '*.ico') $installedIcons -Force

$regLines = @('Windows Registry Editor Version 5.00')
$dllEscaped = $installedDll -replace '\\', '\\'
foreach ($color in $Colors) {
    $entry = $overlayClasses[$color]
    $regLines += ''
    $regLines += "[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$($entry.Clsid)]"
    $regLines += ('@="{0}"' -f $entry.Name)
    $regLines += "[HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$($entry.Clsid)\InprocServer32]"
    $regLines += ('@="{0}"' -f $dllEscaped)
    $regLines += '"ThreadingModel"="Apartment"'
    $regLines += "[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\$($entry.Name)]"
    $regLines += ('@="{0}"' -f $entry.Clsid)
}

$regFile = Join-Path $env:TEMP 'colortags_icon_overlays.reg'
$regLines | Set-Content -Path $regFile -Encoding Ascii
$process = Start-Process reg.exe -ArgumentList 'import', "`"$regFile`"" -Verb RunAs -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "reg.exe exited with code $($process.ExitCode)" }
Remove-Item -LiteralPath $regFile -Force -ErrorAction SilentlyContinue

Write-Output "Registered $($Colors.Count) overlay handler(s): $($Colors -join ', ')"
Write-Output "  DLL:   $installedDll"
Write-Output "  Icons: $installedIcons"
Write-Output 'Explorer reads this list only at startup - it must be restarted.'

if ($RestartExplorer) {
    Write-Output 'Restarting Explorer...'
    Stop-Process -Name explorer -Force
    Start-Sleep -Seconds 2
    Start-Process explorer
}
