# Prepare a self-contained release. SDK tools are used only on the build machine.
param(
    [string]$PackageRoot = 'package',
    [string]$SdkTools,
    [string]$PythonArchive
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$scratch = Join-Path $repoRoot ('temp\installer-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$cert = $null
try {
    if (-not $SdkTools) {
        $kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
        $SdkTools = Get-ChildItem $kit -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'x64' } |
            Where-Object { Test-Path (Join-Path $_ 'makeappx.exe') } | Select-Object -First 1
    }
    foreach ($tool in @('makeappx.exe','signtool.exe')) {
        if (-not $SdkTools -or -not (Test-Path (Join-Path $SdkTools $tool))) {
            throw "Build tool $tool is missing. Pass -SdkTools."
        }
    }
    $runtime = Join-Path $package 'Runtime'
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null
    if (-not $PythonArchive) {
        $PythonArchive = Join-Path $scratch 'python.zip'
        Invoke-WebRequest 'https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip' -OutFile $PythonArchive
    }
    $expectedHash = 'D297E5FF019966817AD8502465176139F2D3D840FA4ED84B13BED399A6AB1F15'
    if ((Get-FileHash -LiteralPath $PythonArchive -Algorithm SHA256).Hash -ne $expectedHash) {
        throw 'Python runtime archive checksum mismatch.'
    }
    Expand-Archive -LiteralPath $PythonArchive -DestinationPath $runtime -Force
    # Isolated interpreter: only stdlib, runtime DLLs and our application root.
    "python314.zip`n.`n.." | Set-Content (Join-Path $runtime 'python314._pth') -Encoding ASCII
    & (Join-Path $runtime 'python.exe') -B -m src.Cli.colortag list
    if ($LASTEXITCODE -ne 0) { throw 'Embedded Python cannot load the CLI.' }

    $payload = Join-Path $scratch 'msix'
    New-Item -ItemType Directory -Path $payload | Out-Null
    Copy-Item (Join-Path $repoRoot 'src\ShellExtension\out\*') $payload -Recurse
    Get-ChildItem $payload -Filter 'ColorTagsSchemaTool*.exe' | Remove-Item -Force
    $manifestPath = Join-Path $payload 'AppxManifest.xml'
    [xml]$manifest = Get-Content -LiteralPath $manifestPath
    $version = (Get-Content (Join-Path $repoRoot 'VERSION') -Raw).Trim()
    $manifest.Package.Identity.Version = "$version.0"
    $manifest.Save($manifestPath)
    $shell = Join-Path $package 'ShellExtension'
    Copy-Item $manifestPath (Join-Path $shell 'AppxManifest.xml') -Force
    $msix = Join-Path $shell 'ColorTags.msix'
    & (Join-Path $SdkTools 'makeappx.exe') pack /d $payload /p $msix /o
    if ($LASTEXITCODE -ne 0) { throw 'MSIX packing failed.' }
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=ColorTags' `
        -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)
    Export-Certificate -Cert $cert -FilePath (Join-Path $shell 'ColorTags.cer') | Out-Null
    $password = [Guid]::NewGuid().ToString('N') + '!Aa1'
    $pfx = Join-Path $scratch 'signing.pfx'
    Export-PfxCertificate -Cert $cert -FilePath $pfx `
        -Password (ConvertTo-SecureString $password -AsPlainText -Force) | Out-Null
    & (Join-Path $SdkTools 'signtool.exe') sign /fd SHA256 /f $pfx /p $password $msix
    if ($LASTEXITCODE -ne 0) { throw 'MSIX signing failed.' }
    Write-Output 'Self-contained installer payload prepared.'
} finally {
    if ($cert) { Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -Force }
    # Scratch is created under this repository, with a unique fixed prefix.
    if (-not $scratch.StartsWith((Join-Path $repoRoot 'temp\installer-'))) { throw 'Unsafe scratch path.' }
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
