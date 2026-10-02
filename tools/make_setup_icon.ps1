# Convert the supplied installer artwork to a multi-size PNG-backed ICO.
# Uses only Windows System.Drawing; ordinary builds consume the committed ICO.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'installer\assets\setup-icon.png'
$destination = Join-Path $root 'installer\assets\setup-icon.ico'
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$inputImage = [System.Drawing.Image]::FromFile($source)
$payloads = New-Object 'System.Collections.Generic.List[byte[]]'
try {
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($inputImage, 0, 0, $size, $size)
            } finally { $graphics.Dispose() }
            $stream = New-Object System.IO.MemoryStream
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $payloads.Add($stream.ToArray())
            } finally { $stream.Dispose() }
        } finally { $bitmap.Dispose() }
    }
} finally { $inputImage.Dispose() }

$file = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create)
try {
    $writer = New-Object System.IO.BinaryWriter($file)
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $encodedSize = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$encodedSize)
        $writer.Write([byte]$encodedSize)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$payloads[$i].Length)
        $writer.Write([uint32]$offset)
        $offset += $payloads[$i].Length
    }
    foreach ($payload in $payloads) { $writer.Write([byte[]]$payload) }
} finally { $file.Dispose() }
Write-Output "Installer icon: $destination"
