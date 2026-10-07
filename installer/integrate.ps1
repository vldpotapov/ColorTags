param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
# A setup launched from PowerShell 7 inherits its module search path. Windows
# PowerShell 5 must load its own Security/Appx modules, not PowerShell 7 copies.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' +
    (Join-Path $env:ProgramFiles 'WindowsPowerShell\Modules')
$appRoot = Split-Path -Parent $PSScriptRoot
$logRoot = Join-Path $env:LOCALAPPDATA 'Colortags'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$log = Join-Path $logRoot 'setup-integration.log'
try {
    Start-Transcript -Path $log -Force | Out-Null
    if ($Uninstall) {
        & (Join-Path $appRoot 'ShellExtension\unregister.ps1')
    } else {
        & (Join-Path $appRoot 'ShellExtension\register.ps1') `
            -PythonPath (Join-Path $appRoot 'Runtime\python.exe')
        if (-not (Get-AppxPackage -Name 'ColorTags.ExplorerMenu')) {
            throw 'Explorer menu package was not registered.'
        }
    }
    Stop-Transcript | Out-Null
    exit 0
} catch {
    Write-Output $_
    Stop-Transcript -ErrorAction SilentlyContinue | Out-Null
    exit 1
}
