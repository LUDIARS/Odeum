[CmdletBinding()]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'Programs\Spectator')
)

$ErrorActionPreference = 'Stop'
$packageDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourceExecutable = Join-Path $packageDirectory 'Spectator.exe'
$sourceSettings = Join-Path $packageDirectory 'spectator.settings.json'
$sourceUninstaller = Join-Path $packageDirectory 'Uninstall-Spectator.ps1'

if (
    -not (Test-Path -LiteralPath $sourceExecutable) -or
    -not (Test-Path -LiteralPath $sourceSettings) -or
    -not (Test-Path -LiteralPath $sourceUninstaller)
) {
    throw 'Spectator.exe, spectator.settings.json, and Uninstall-Spectator.ps1 must be beside this installer.'
}
if (Get-Process -Name Spectator -ErrorAction SilentlyContinue) {
    throw 'Close Spectator before installing it.'
}

New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
Copy-Item -LiteralPath $sourceExecutable -Destination (Join-Path $InstallDirectory 'Spectator.exe') -Force
$installedSettings = Join-Path $InstallDirectory 'spectator.settings.json'
if (-not (Test-Path -LiteralPath $installedSettings)) {
    Copy-Item -LiteralPath $sourceSettings -Destination $installedSettings
}
Copy-Item -LiteralPath $sourceUninstaller -Destination (Join-Path $InstallDirectory 'Uninstall-Spectator.ps1') -Force

$startMenuDirectory = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
New-Item -ItemType Directory -Path $startMenuDirectory -Force | Out-Null
$shortcutPath = Join-Path $startMenuDirectory 'Spectator.lnk'
$shell = New-Object -ComObject WScript.Shell
try {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = Join-Path $InstallDirectory 'Spectator.exe'
    $shortcut.WorkingDirectory = $InstallDirectory
    $shortcut.Description = 'Volputas game capture satellite'
    $shortcut.Save()
} finally {
    if ($shortcut) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shortcut) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
}

$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Spectator'
New-Item -Path $uninstallKey -Force | Out-Null
Set-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'Spectator'
Set-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '0.1.0'
Set-ItemProperty -Path $uninstallKey -Name Publisher -Value 'LUDIARS'
Set-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $InstallDirectory
$uninstallCommand = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDirectory 'Uninstall-Spectator.ps1')`""
Set-ItemProperty -Path $uninstallKey -Name UninstallString -Value $uninstallCommand
New-ItemProperty -Path $uninstallKey -Name NoModify -PropertyType DWord -Value 1 -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -PropertyType DWord -Value 1 -Force | Out-Null

Write-Host "Spectator was installed to $InstallDirectory."
