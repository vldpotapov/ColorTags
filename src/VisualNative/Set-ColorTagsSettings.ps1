<#
.SYNOPSIS
    Reads and writes the ColorTags display settings.

.DESCRIPTION
    The settings live in HKCU\Software\ColorTags, next to the PythonPath and
    ProjectRoot values that register.ps1 writes:

        DisplayMode   REG_DWORD   0 = colored dot, 1 = colored label
        Labels\<id>   REG_SZ      the user's name for that tag

    Renaming a tag changes presentation only. The stable ids "red" ... "gray"
    stay in the :ColorTag stream of every tagged file, so nothing on disk is
    rewritten and no tag is lost.

    The running overlay reloads these values on its next scan pass, and the
    context menu reads them the next time it is opened. Neither needs a
    restart. Explorer caches column values, so a renamed tag may keep its old
    text in the Tags column until the folder is refreshed with F5.

    The tray icon's settings window does all of this with a mouse; this
    script stays for scripted setup and for checking the stored values.

.EXAMPLE
    .\Set-ColorTagsSettings.ps1 -Show

.EXAMPLE
    .\Set-ColorTagsSettings.ps1 -Mode Label -Label @{ red = 'Urgent'; green = 'Done' }

.EXAMPLE
    .\Set-ColorTagsSettings.ps1 -Reset
#>
[CmdletBinding(DefaultParameterSetName = 'Set')]
param(
    [Parameter(ParameterSetName = 'Set')]
    [ValidateSet('Dot', 'Label')]
    [string] $Mode,

    [Parameter(ParameterSetName = 'Set')]
    [hashtable] $Label,

    [Parameter(ParameterSetName = 'Set')]
    [switch] $Reset,

    [Parameter(ParameterSetName = 'Show')]
    [switch] $Show
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$configKey = 'HKCU:\Software\ColorTags'
$labelsKey = 'HKCU:\Software\ColorTags\Labels'

# Must match src/Shared/ColorTagsConfig.h. The ids are the storage contract.
$defaults = [ordered]@{
    red    = 'Red'
    orange = 'Orange'
    yellow = 'Yellow'
    green  = 'Green'
    blue   = 'Blue'
    purple = 'Purple'
    gray   = 'Gray'
}

$maxLabelLength = 24

function Get-Settings {
    $mode = 'Dot'
    if (Test-Path $configKey) {
        $value = (Get-ItemProperty -Path $configKey -Name 'DisplayMode' -ErrorAction SilentlyContinue).DisplayMode
        if ($value -eq 1) { $mode = 'Label' }
    }
    $labels = [ordered]@{}
    foreach ($id in $defaults.Keys) {
        $current = $null
        if (Test-Path $labelsKey) {
            # Set-StrictMode turns a missing value into a PropertyNotFound
            # error, so ask the key what it holds instead of dotting into it.
            $item = Get-Item -Path $labelsKey
            if ($item.GetValueNames() -contains $id) { $current = $item.GetValue($id) }
        }
        if ([string]::IsNullOrWhiteSpace($current)) { $current = $defaults[$id] }
        $labels[$id] = $current
    }
    [pscustomobject]@{ Mode = $mode; Labels = $labels }
}

function Show-Settings {
    $settings = Get-Settings
    Write-Host "Display mode: $($settings.Mode)"
    Write-Host 'Labels:'
    foreach ($id in $settings.Labels.Keys) {
        $suffix = if ($settings.Labels[$id] -eq $defaults[$id]) { ' (default)' } else { '' }
        Write-Host ("  {0,-7} {1}{2}" -f $id, $settings.Labels[$id], $suffix)
    }
}

if ($Show) {
    Show-Settings
    return
}

if ($Reset) {
    if (Test-Path $labelsKey) { Remove-Item -Path $labelsKey -Recurse -Force }
    if (Test-Path $configKey) {
        Remove-ItemProperty -Path $configKey -Name 'DisplayMode' -ErrorAction SilentlyContinue
    }
    Write-Host 'Display mode and labels restored to defaults.'
    Show-Settings
    return
}

if (-not $PSBoundParameters.ContainsKey('Mode') -and -not $PSBoundParameters.ContainsKey('Label')) {
    Show-Settings
    return
}

if (-not (Test-Path $configKey)) { New-Item -Path $configKey -Force | Out-Null }

if ($PSBoundParameters.ContainsKey('Mode')) {
    $value = if ($Mode -eq 'Label') { 1 } else { 0 }
    New-ItemProperty -Path $configKey -Name 'DisplayMode' -Value $value `
        -PropertyType DWord -Force | Out-Null
}

if ($PSBoundParameters.ContainsKey('Label')) {
    if (-not (Test-Path $labelsKey)) { New-Item -Path $labelsKey -Force | Out-Null }
    foreach ($id in $Label.Keys) {
        $key = [string]$id
        if (-not $defaults.Contains($key.ToLowerInvariant())) {
            throw "Unknown tag id '$key'. Expected one of: $($defaults.Keys -join ', ')."
        }
        $key = $key.ToLowerInvariant()
        $text = [string]$Label[$id]
        # Control characters would corrupt a menu item and a column cell alike.
        $text = ($text -replace '[\x00-\x1F\x7F]', '').Trim()
        if ($text.Length -gt $maxLabelLength) { $text = $text.Substring(0, $maxLabelLength) }
        # An empty label restores the default instead of blanking the menu item.
        New-ItemProperty -Path $labelsKey -Name $key -Value $text `
            -PropertyType String -Force | Out-Null
    }
}

Show-Settings
