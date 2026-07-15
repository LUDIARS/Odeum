[CmdletBinding()]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\Spectator'),
    [switch]$PurgeUserData
)

$ErrorActionPreference = 'Stop'
if (Get-Process -Name Spectator -ErrorAction SilentlyContinue) {
    throw 'Close Spectator before uninstalling it.'
}

$resolvedParent = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs'))
$resolvedInstall = [System.IO.Path]::GetFullPath($InstallDirectory)
if (-not $resolvedInstall.StartsWith($resolvedParent + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The install directory is outside the expected location: $resolvedInstall"
}

$shortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Spectator.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath -Force
}
Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Spectator' -Recurse -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $resolvedInstall) {
    Remove-Item -LiteralPath $resolvedInstall -Recurse -Force
}

if ($PurgeUserData) {
    $dataDirectory = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Spectator'))
    if (Test-Path -LiteralPath $dataDirectory) {
        Remove-Item -LiteralPath $dataDirectory -Recurse -Force
    }
}

Write-Host 'Spectator was uninstalled. User data is retained unless -PurgeUserData is specified.'
