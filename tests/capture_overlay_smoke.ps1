param(
    [Parameter(Mandatory = $true)]
    [string]$Folder,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath,
    [string]$ContextMenuItem,
    [switch]$ForceTop
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class ColorTagsCaptureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr hwnd, IntPtr insertAfter, int x, int y, int width, int height,
        uint flags);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hwnd, int command);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(
        uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    public static extern void keybd_event(
        byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
}
'@

$resolvedFolder = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$shell = New-Object -ComObject Shell.Application
$window = @($shell.Windows()) | Where-Object {
    try {
        $_.FullName -like '*explorer.exe' -and
        $_.Document.Folder.Self.Path.TrimEnd('\') -ieq $resolvedFolder
    } catch { $false }
} | Select-Object -First 1
if (-not $window) { throw "No Explorer window is open at $resolvedFolder" }

$hwnd = [IntPtr]::new([int64]$window.HWND)
$previousForeground = [ColorTagsCaptureNative]::GetForegroundWindow()
[ColorTagsCaptureNative]::ShowWindow($hwnd, 9) | Out-Null
if ($ForceTop) {
    [ColorTagsCaptureNative]::SetWindowPos(
        $hwnd, [IntPtr]::Zero, 0, 0, 0, 0, 0x0003
    ) | Out-Null
}
[ColorTagsCaptureNative]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 750

if ($ContextMenuItem) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty,
        $ContextMenuItem
    )
    $row = $root.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $condition
    )
    if (-not $row) { throw "Explorer row not found: $ContextMenuItem" }
    $rowBounds = $row.Current.BoundingRectangle
    [ColorTagsCaptureNative]::SetCursorPos(
        [int]($rowBounds.Left + 80),
        [int](($rowBounds.Top + $rowBounds.Bottom) / 2)
    ) | Out-Null
    [ColorTagsCaptureNative]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)
    [ColorTagsCaptureNative]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900
}

$rect = [ColorTagsCaptureNative+RECT]::new()
if (-not [ColorTagsCaptureNative]::GetWindowRect($hwnd, [ref]$rect)) {
    throw 'GetWindowRect failed.'
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = [System.Drawing.Bitmap]::new($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $destination = [System.IO.Path]::GetFullPath($OutputPath)
    $bitmap.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output $destination
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

if ($ContextMenuItem) {
    [ColorTagsCaptureNative]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
    [ColorTagsCaptureNative]::keybd_event(0x1B, 0, 0x0002, [UIntPtr]::Zero)
}

if ($ForceTop -and $previousForeground -ne [IntPtr]::Zero -and
    $previousForeground -ne $hwnd) {
    [ColorTagsCaptureNative]::SetWindowPos(
        $previousForeground, [IntPtr]::Zero, 0, 0, 0, 0, 0x0003
    ) | Out-Null
    [ColorTagsCaptureNative]::SetForegroundWindow($previousForeground) | Out-Null
}
