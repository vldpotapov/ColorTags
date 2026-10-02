<#
.SYNOPSIS
    Removes the ColorTags icon overlay handlers.

.DESCRIPTION
    Symmetric to register_icon_overlays.ps1: deletes both the entries under
    ShellIconOverlayIdentifiers and the COM classes behind them, leaving no
    dangling CLSID. Needs elevation; Explorer must be restarted afterwards.
#>
[CmdletBinding()]
param(
    [switch] $RestartExplorer
)

$ErrorActionPreference = 'Stop'

$overlayClasses = [ordered]@{
    red    = @{ Clsid = '{CC46952F-1C86-46B4-8C49-AA90F8DBDA56}'; Name = 'ColorTags Red' }
    orange = @{ Clsid = '{580D6C3D-7C45-4EEF-8C80-085B852B5C74}'; Name = 'ColorTags Orange' }
    yellow = @{ Clsid = '{D9D4BE22-2CF8-4F7E-B9E7-4F7500DD4365}'; Name = 'ColorTags Yellow' }
    green  = @{ Clsid = '{523F13B4-B99A-46B8-829A-FB9E0E6A6C0D}'; Name = 'ColorTags Green' }
    blue   = @{ Clsid = '{E8889757-2B0D-4C63-B867-9FC5C9AD946E}'; Name = 'ColorTags Blue' }
    purple = @{ Clsid = '{C9A70174-6FDB-4E4A-AFCA-1F3733325CBD}'; Name = 'ColorTags Purple' }
    gray   = @{ Clsid = '{3E8FF435-6341-43BD-A962-0DA454E1B9CD}'; Name = 'ColorTags Gray' }
}

$regLines = @('Windows Registry Editor Version 5.00')
foreach ($color in $overlayClasses.Keys) {
    $entry = $overlayClasses[$color]
    $regLines += ''
    $regLines += "[-HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\$($entry.Name)]"
    $regLines += "[-HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$($entry.Clsid)]"
}

$regFile = Join-Path $env:TEMP 'colortags_icon_overlays_remove.reg'
$regLines | Set-Content -Path $regFile -Encoding Ascii
$process = Start-Process reg.exe -ArgumentList 'import', "`"$regFile`"" -Verb RunAs -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "reg.exe exited with code $($process.ExitCode)" }
Remove-Item -LiteralPath $regFile -Force -ErrorAction SilentlyContinue

Write-Output 'Removed all ColorTags icon overlay handlers.'
Write-Output 'Explorer must be restarted for this to take effect.'

if ($RestartExplorer) {
    Write-Output 'Restarting Explorer...'
    Stop-Process -Name explorer -Force
    Start-Sleep -Seconds 2
    Start-Process explorer
}
