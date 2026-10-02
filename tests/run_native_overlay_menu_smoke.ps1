param([string]$Folder = (Join-Path $env:USERPROFILE 'Downloads'))

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeMenuSmoke {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(
        EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(
        IntPtr hwnd, StringBuilder value, int maximum);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT point);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint x, uint y, int data, UIntPtr extra);
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
$process = $null
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
    $rowCondition = New-Object System.Windows.Automation.OrCondition(
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::DataItem
        )),
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::ListItem
        ))
    )
    $row = $automationRoot.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants, $rowCondition
    )
    if (-not $row) { throw 'No visible file row was found.' }

    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--target-folder', ('"{0}"' -f $resolvedFolder)
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200
    [NativeMenuSmoke]::SetWindowPos(
        $explorerHwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0003
    ) | Out-Null
    [NativeMenuSmoke]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [NativeMenuSmoke]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [NativeMenuSmoke]::SetForegroundWindow($explorerHwnd) | Out-Null
    Start-Sleep -Milliseconds 150
    $overlay = [NativeMenuSmoke]::FindOverlay($explorerHwnd)
    if ($overlay -eq [IntPtr]::Zero -or -not [NativeMenuSmoke]::IsWindowVisible($overlay)) {
        throw 'Overlay was not visible before opening the context menu.'
    }

    $bounds = $row.Current.BoundingRectangle
    $point = [NativeMenuSmoke+POINT]::new()
    $point.X = [int](($bounds.Left + $bounds.Right) / 2)
    $point.Y = [int](($bounds.Top + $bounds.Bottom) / 2)
    [NativeMenuSmoke]::SetCursorPos($point.X, $point.Y) | Out-Null
    $pointRoot = [NativeMenuSmoke]::GetAncestor(
        [NativeMenuSmoke]::WindowFromPoint($point), 2
    )
    if ($pointRoot -ne $explorerHwnd) {
        throw "Test point belongs to HWND $pointRoot instead of Explorer $explorerHwnd."
    }
    [NativeMenuSmoke]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)
    [NativeMenuSmoke]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    if (-not [NativeMenuSmoke]::IsWindowVisible($overlay)) {
        throw 'Context menu unexpectedly hid the overlay.'
    }

    [NativeMenuSmoke]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
    [NativeMenuSmoke]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
    $restoreWatch = [System.Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 10
    } while ($restoreWatch.ElapsedMilliseconds -lt 1500 -and
             -not [NativeMenuSmoke]::IsWindowVisible($overlay))
    if (-not [NativeMenuSmoke]::IsWindowVisible($overlay)) {
        throw 'Overlay did not return after the context menu closed.'
    }

    [pscustomobject]@{
        VisibleDuringMenu = $true
        RestoreMilliseconds = $restoreWatch.ElapsedMilliseconds
        ExplorerResponsive = (@(Get-Process explorer | Where-Object { -not $_.Responding }).Count -eq 0)
    }
} finally {
    [NativeMenuSmoke]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
    [NativeMenuSmoke]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
    if ($explorerHwnd -ne [IntPtr]::Zero) {
        [NativeMenuSmoke]::SetWindowPos(
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
}
