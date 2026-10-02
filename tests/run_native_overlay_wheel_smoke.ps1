param([string]$Folder = (Join-Path $env:USERPROFILE 'Downloads'))

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeWheelSmoke {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] public struct RECT {
        public int Left, Top, Right, Bottom;
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(
        EnumWindowsProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(
        IntPtr hwnd, StringBuilder value, int maximum);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr hwnd, uint command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(
        IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(
        uint flags, uint x, uint y, int data, UIntPtr extra);
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
$status = Join-Path $env:TEMP "colortags-native-wheel-$PID.json"
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
    Remove-Item -LiteralPath $status -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--target-folder', ('"{0}"' -f $resolvedFolder),
        '--status', ('"{0}"' -f $status),
        '--diagnostic-fast-status'
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1200

    $explorerRect = [NativeWheelSmoke+RECT]::new()
    [NativeWheelSmoke]::GetWindowRect($explorerHwnd, [ref]$explorerRect) | Out-Null
    [NativeWheelSmoke]::SetWindowPos(
        $explorerHwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0003
    ) | Out-Null
    [NativeWheelSmoke]::SetForegroundWindow($explorerHwnd) | Out-Null
    [NativeWheelSmoke]::SetCursorPos(
        [int]($explorerRect.Left + (($explorerRect.Right - $explorerRect.Left) * 0.75)),
        [int]($explorerRect.Top + (($explorerRect.Bottom - $explorerRect.Top) * 0.65))
    ) | Out-Null
    Start-Sleep -Milliseconds 150
    $overlay = [NativeWheelSmoke]::FindOverlay($explorerHwnd)
    if ($overlay -eq [IntPtr]::Zero) { throw 'Overlay HWND was not found.' }
    $initialState = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    $wheelDelta = if ($initialState.scrollPercent -ge 99) { 120 } else { -120 }
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    [NativeWheelSmoke]::mouse_event(0x0800, 0, 0, $wheelDelta, [UIntPtr]::Zero)
    do {
        Start-Sleep -Milliseconds 1
    } while ($stopwatch.ElapsedMilliseconds -lt 100 -and
             [NativeWheelSmoke]::IsWindowVisible($overlay))
    if ([NativeWheelSmoke]::IsWindowVisible($overlay)) {
        throw 'The overlay was not hidden within 100 ms of a real wheel scroll.'
    }
    $hideMilliseconds = $stopwatch.ElapsedMilliseconds
    1..2 | ForEach-Object {
        [NativeWheelSmoke]::mouse_event(0x0800, 0, 0, $wheelDelta, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 5
    }
    $restoreWatch = [System.Diagnostics.Stopwatch]::StartNew()
    do {
        Start-Sleep -Milliseconds 5
        $overlay = [NativeWheelSmoke]::FindOverlay($explorerHwnd)
    } while ($restoreWatch.ElapsedMilliseconds -lt 1500 -and
             ($overlay -eq [IntPtr]::Zero -or
              -not [NativeWheelSmoke]::IsWindowVisible($overlay)))
    if ($overlay -eq [IntPtr]::Zero -or
        -not [NativeWheelSmoke]::IsWindowVisible($overlay)) {
        throw 'The overlay was not restored after wheel scrolling settled.'
    }
    $restoreMilliseconds = $restoreWatch.ElapsedMilliseconds
    Start-Sleep -Milliseconds 150

    $state = Get-Content -LiteralPath $status -Raw | ConvertFrom-Json
    if ($state.wheelEvents -lt 1) { throw 'The low-level wheel hook received no event.' }
    [pscustomobject]@{
        WheelEvents = $state.wheelEvents
        FastScans = $state.fastScans
        HideMilliseconds = $hideMilliseconds
        RestoreMilliseconds = $restoreMilliseconds
        RowPitch = $initialState.rowPitch
        InitialScrollPercent = $initialState.scrollPercent
        FinalScrollPercent = $state.scrollPercent
        WheelDelta = $wheelDelta
        ExplorerResponsive = (@(Get-Process explorer | Where-Object { -not $_.Responding }).Count -eq 0)
    }
} finally {
    if ($explorerHwnd -ne [IntPtr]::Zero) {
        [NativeWheelSmoke]::SetWindowPos(
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
