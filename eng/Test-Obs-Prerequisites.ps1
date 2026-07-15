[CmdletBinding()]
param(
    [switch]$RequireRunning
)

$ErrorActionPreference = 'Stop'
$minimumVersion = [Version]'28.0.0'
$candidates = @(@(
    (Join-Path $env:ProgramFiles 'obs-studio\bin\64bit\obs64.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'obs-studio\bin\64bit\obs64.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) })

if ($candidates.Count -eq 0) { throw 'OBS Studio 28 or later is not installed in a standard location.' }
$executable = (Resolve-Path -LiteralPath $candidates[0]).Path
$fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable).FileVersion
$versionMatch = [regex]::Match($fileVersion, '^\d+(\.\d+){1,3}')
if (-not $versionMatch.Success) { throw "OBS version could not be parsed: $fileVersion" }
$version = [Version]$versionMatch.Value
if ($version -lt $minimumVersion) { throw "OBS $version is older than required version $minimumVersion." }

$process = Get-Process obs64 -ErrorAction SilentlyContinue | Select-Object -First 1
$listener = Get-NetTCPConnection -LocalPort 4455 -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
$result = [ordered]@{
    installed = $true
    executable = $executable
    version = $version.ToString()
    running = $null -ne $process
    websocket4455Listening = $null -ne $listener
    processId = if ($process) { $process.Id } else { $null }
}
$result | ConvertTo-Json -Compress

if ($RequireRunning -and (-not $result.running -or -not $result.websocket4455Listening)) {
    throw 'OBS must be running with WebSocket server port 4455 enabled for the recording smoke test.'
}
