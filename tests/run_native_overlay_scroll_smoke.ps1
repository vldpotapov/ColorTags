param(
    [string]$Folder = (Join-Path $env:USERPROFILE 'Downloads')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$rootDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $rootDirectory 'src\VisualNative\out\ColorTagsOverlay.exe'
$resolvedFolder = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$status = Join-Path $env:TEMP "colortags-native-scroll-$PID.json"
$process = $null
$pattern = $null
$originalScroll = $null

try {
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--target-folder', ('"{0}"' -f $resolvedFolder),
        '--status', ('"{0}"' -f $status)
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200

    $shell = New-Object -ComObject Shell.Application
    $window = @($shell.Windows()) | Where-Object {
        try {
            $_.FullName -like '*explorer.exe' -and
            $_.Document.Folder.Self.Path.TrimEnd('\') -ieq $resolvedFolder
        } catch { $false }
    } | Select-Object -First 1
    if (-not $window) { throw "No Explorer window is open at $resolvedFolder" }

    $automationRoot = [System.Windows.Automation.AutomationElement]::FromHandle(
        [IntPtr]::new([int64]$window.HWND)
    )
    $condition = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::IsScrollPatternAvailableProperty,
            $true
        )),
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty,
            'Items View'
        ))
    )
    $itemsView = $automationRoot.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $condition
    )
    if (-not $itemsView) { throw 'Scrollable Items View was not found.' }
    $pattern = [System.Windows.Automation.ScrollPattern]$itemsView.GetCurrentPattern(
        [System.Windows.Automation.ScrollPattern]::Pattern
    )
    if (-not $pattern.Current.VerticallyScrollable) { throw 'Items View is not scrollable.' }
    $originalScroll = $pattern.Current.VerticalScrollPercent
    $pattern.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, 0)
    Start-Sleep -Milliseconds 700
    $before = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($before.windows -lt 1 -or $before.dots -lt 1) {
        throw 'Tagged rows were not visible at the top before the recovery test.'
    }
    $pattern.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, 100)
    Start-Sleep -Milliseconds 900
    $after = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($after.contentEvents -le $before.contentEvents) {
        throw 'Scrolling produced no content event.'
    }
    $pattern.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, 0)
    Start-Sleep -Milliseconds 1200
    $recovered = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($recovered.windows -lt 1 -or $recovered.dots -lt $before.dots) {
        throw 'Overlay did not rebuild after returning from the bottom.'
    }
    [pscustomobject]@{
        ContentEvents = $after.contentEvents - $before.contentEvents
        FastScans = $after.fastScans - $before.fastScans
        RecoveredDots = $recovered.dots
        ExplorerResponsive = (@(Get-Process explorer | Where-Object { -not $_.Responding }).Count -eq 0)
    }
} finally {
    if ($pattern -and $null -ne $originalScroll) {
        $pattern.SetScrollPercent(
            [System.Windows.Automation.ScrollPattern]::NoScroll,
            $originalScroll
        )
    }
    if ($process -and -not $process.HasExited) {
        try {
            $stopEvent = [System.Threading.EventWaitHandle]::OpenExisting(
                'Local\ColorTags.NativeOverlay.Stop'
            )
            $stopEvent.Set() | Out-Null
            $stopEvent.Dispose()
        } catch { }
        if (-not $process.WaitForExit(3000)) { Stop-Process -Id $process.Id -Force }
    }
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
}
