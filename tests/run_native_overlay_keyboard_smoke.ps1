param([string]$Folder = (Join-Path $env:USERPROFILE 'Downloads'))

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeKeyboardSmoke {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(
        EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(
        IntPtr hwnd, StringBuilder value, int maximum);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(
        byte virtualKey, byte scanCode, uint flags, UIntPtr extra);
    public static IntPtr FindOverlay(IntPtr owner) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((hwnd, parameter) => {
            StringBuilder name = new StringBuilder(128);
            GetClassName(hwnd, name, name.Capacity);
            if (name.ToString() == "ColorTags.NativeOverlay.Window" &&
                GetWindow(hwnd, 4) == owner) {
                result = hwnd;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
'@

$rootDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $rootDirectory 'src\VisualNative\out\ColorTagsOverlay.exe'
$resolvedFolder = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$status = Join-Path $env:TEMP "colortags-native-keyboard-$PID.json"
$process = $null
$pattern = $null
$originalScroll = $null
$explorerHwnd = [IntPtr]::Zero

try {
    $shell = New-Object -ComObject Shell.Application
    $window = @($shell.Windows()) | Where-Object {
        try {
            $_.FullName -like '*explorer.exe' -and
            $_.Document.Folder.Self.Path.TrimEnd('\') -ieq $resolvedFolder
        } catch { $false }
    } | Select-Object -First 1
    if (-not $window) { throw "No Explorer window is open at $resolvedFolder" }
    $explorerHwnd = [IntPtr]::new([int64]$window.HWND)
    $automationRoot = [System.Windows.Automation.AutomationElement]::FromHandle($explorerHwnd)
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
        [System.Windows.Automation.TreeScope]::Descendants, $condition
    )
    if (-not $itemsView) { throw 'Scrollable Items View was not found.' }
    $pattern = [System.Windows.Automation.ScrollPattern]$itemsView.GetCurrentPattern(
        [System.Windows.Automation.ScrollPattern]::Pattern
    )
    if (-not $pattern.Current.VerticallyScrollable) { throw 'Items View is not scrollable.' }
    $originalScroll = $pattern.Current.VerticalScrollPercent
    $pattern.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, 0)

    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--target-folder', ('"{0}"' -f $resolvedFolder),
        '--status', ('"{0}"' -f $status),
        '--diagnostic-fast-status'
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200

    [NativeKeyboardSmoke]::SetWindowPos(
        $explorerHwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0003
    ) | Out-Null
    [NativeKeyboardSmoke]::SetForegroundWindow($explorerHwnd) | Out-Null
    Start-Sleep -Milliseconds 150
    $overlay = [NativeKeyboardSmoke]::FindOverlay($explorerHwnd)
    if ($overlay -eq [IntPtr]::Zero) { throw 'Overlay HWND was not found.' }
    if (-not [NativeKeyboardSmoke]::IsWindowVisible($overlay)) {
        throw 'Overlay was not visible before PageDown.'
    }

    $before = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    [NativeKeyboardSmoke]::keybd_event(0x22, 0, 0, [UIntPtr]::Zero)
    [NativeKeyboardSmoke]::keybd_event(0x22, 0, 2, [UIntPtr]::Zero)
    do {
        Start-Sleep -Milliseconds 1
    } while ($stopwatch.ElapsedMilliseconds -lt 100 -and
             [NativeKeyboardSmoke]::IsWindowVisible($overlay))
    if ([NativeKeyboardSmoke]::IsWindowVisible($overlay)) {
        throw 'PageDown did not hide the overlay within 100 ms.'
    }
    $hideMilliseconds = $stopwatch.ElapsedMilliseconds

    Start-Sleep -Milliseconds 250
    [NativeKeyboardSmoke]::keybd_event(0x24, 0, 0, [UIntPtr]::Zero)
    [NativeKeyboardSmoke]::keybd_event(0x24, 0, 2, [UIntPtr]::Zero)
    $restoreWatch = [System.Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 10
        $overlay = [NativeKeyboardSmoke]::FindOverlay($explorerHwnd)
    } while ($restoreWatch.ElapsedMilliseconds -lt 2500 -and
             ($overlay -eq [IntPtr]::Zero -or
              -not [NativeKeyboardSmoke]::IsWindowVisible($overlay)))
    if ($overlay -eq [IntPtr]::Zero -or
        -not [NativeKeyboardSmoke]::IsWindowVisible($overlay)) {
        throw 'Overlay did not return after Home restored the tagged rows.'
    }
    $after = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($after.contentEvents -le $before.contentEvents) {
        throw 'PageDown produced no content event.'
    }
    if ($after.fastScans -le $before.fastScans) {
        throw 'PageDown produced no cached fast scan.'
    }

    [pscustomobject]@{
        HideMilliseconds = $hideMilliseconds
        HomeRestoreMilliseconds = $restoreWatch.ElapsedMilliseconds
        ScrollPercent = $pattern.Current.VerticalScrollPercent
        ContentEvents = $after.contentEvents - $before.contentEvents
        FastScans = $after.fastScans - $before.fastScans
        ExplorerResponsive = (@(Get-Process explorer | Where-Object { -not $_.Responding }).Count -eq 0)
    }
} finally {
    if ($pattern -and $null -ne $originalScroll) {
        $pattern.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, $originalScroll)
    }
    if ($explorerHwnd -ne [IntPtr]::Zero) {
        [NativeKeyboardSmoke]::SetWindowPos(
            $explorerHwnd, [IntPtr](-2), 0, 0, 0, 0, 0x0003
        ) | Out-Null
    }
    if ($process -and -not $process.HasExited) {
        try {
            $event = [System.Threading.EventWaitHandle]::OpenExisting(
                'Local\ColorTags.NativeOverlay.Stop'
            )
            $event.Set() | Out-Null
            $event.Dispose()
        } catch { }
        if (-not $process.WaitForExit(3000)) { Stop-Process -Id $process.Id -Force }
    }
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
}
