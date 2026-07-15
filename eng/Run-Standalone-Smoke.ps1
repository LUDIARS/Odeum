[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path $repositoryRoot 'artifacts\standalone-smoke'
$videoPath = Join-Path $workDirectory 'review.mp4'

New-Item -ItemType Directory -Path $workDirectory -Force | Out-Null
& ffmpeg -nostdin -y -v error `
    -f lavfi -i 'testsrc=size=320x180:rate=15' `
    -f lavfi -i 'sine=frequency=440:sample_rate=44100' `
    -t 2 -c:v libx264 -pix_fmt yuv420p -c:a aac -movflags +faststart $videoPath
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed with exit code $LASTEXITCODE" }

dotnet run --project (Join-Path $repositoryRoot 'tests\Spectator.Windows.Tests') `
    -c Release -- --standalone-smoke $videoPath
if ($LASTEXITCODE -ne 0) { throw "Standalone smoke failed with exit code $LASTEXITCODE" }
