param(
    [string]$Folder = (Join-Path $env:USERPROFILE 'Downloads')
)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class NativeOverlayMoveTest {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hwnd, StringBuilder value, int maximum);

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr hwnd, uint command);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr hwnd, IntPtr insertAfter, int x, int y, int width, int height,
        uint flags);

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

$root = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $root 'src\VisualNative\out\ColorTagsOverlay.exe'
$resolvedFolder = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$status = Join-Path $env:TEMP "colortags-native-move-$PID.json"
$shell = New-Object -ComObject Shell.Application
$window = @($shell.Windows()) | Where-Object {
    try {
        $_.FullName -like '*explorer.exe' -and
        $_.Document.Folder.Self.Path.TrimEnd('\') -ieq $resolvedFolder
    } catch { $false }
} | Select-Object -First 1
if (-not $window) { throw "No Explorer window is open at $resolvedFolder" }
$explorer = [IntPtr]::new([int64]$window.HWND)
$original = [NativeOverlayMoveTest+RECT]::new()
if (-not [NativeOverlayMoveTest]::GetWindowRect($explorer, [ref]$original)) {
    throw 'Cannot read the Explorer window rectangle.'
}

$process = $null
try {
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--refresh-ms', 500,
        '--target-folder', ('"{0}"' -f $resolvedFolder),
        '--status', ('"{0}"' -f $status)
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1500
    $state = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($state.dots -lt 1) { throw 'No tagged rows were rendered.' }
    $overlay = [NativeOverlayMoveTest]::FindOverlay($explorer)
    if ($overlay -eq [IntPtr]::Zero) { throw 'The native overlay HWND was not found.' }
    $before = [NativeOverlayMoveTest+RECT]::new()
    [NativeOverlayMoveTest]::GetWindowRect($overlay, [ref]$before) | Out-Null

    $width = $original.Right - $original.Left
    $height = $original.Bottom - $original.Top
    if (-not [NativeOverlayMoveTest]::SetWindowPos(
        $explorer, [IntPtr]::Zero, $original.Left + 48, $original.Top,
        $width, $height, 0x0014)) {
        throw 'SetWindowPos failed while moving Explorer.'
    }
    Start-Sleep -Milliseconds 250
    $after = [NativeOverlayMoveTest+RECT]::new()
    [NativeOverlayMoveTest]::GetWindowRect($overlay, [ref]$after) | Out-Null
    $deltaX = $after.Left - $before.Left
    $deltaY = $after.Top - $before.Top
    if ([Math]::Abs($deltaX - 48) -gt 2 -or [Math]::Abs($deltaY) -gt 2) {
        throw "Overlay did not follow Explorer: delta=($deltaX,$deltaY)."
    }
    [pscustomobject]@{
        ExplorerDeltaX = 48
        OverlayDeltaX = $deltaX
        OverlayDeltaY = $deltaY
        Dots = $state.dots
    }
} finally {
    $width = $original.Right - $original.Left
    $height = $original.Bottom - $original.Top
    [NativeOverlayMoveTest]::SetWindowPos(
        $explorer, [IntPtr]::Zero, $original.Left, $original.Top,
        $width, $height, 0x0014
    ) | Out-Null
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
