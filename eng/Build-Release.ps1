[CmdletBinding()]
param(
    [ValidateSet('win-x64', 'win-arm64')]
    [string]$Runtime = 'win-x64',
    [string]$Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.1.0',
    [string]$ArchiveUrl = ''
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactsRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts\release'))
if (-not $artifactsRoot.StartsWith($repositoryRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected artifact path: $artifactsRoot"
}
if (Test-Path -LiteralPath $artifactsRoot) { Remove-Item -LiteralPath $artifactsRoot -Recurse -Force }

$publishDirectory = Join-Path $artifactsRoot "Spectator-$Runtime"
New-Item -ItemType Directory -Path $publishDirectory -Force | Out-Null
dotnet publish (Join-Path $repositoryRoot 'src\Spectator.Windows\Spectator.Windows.csproj') `
    -c $Configuration `
    -r $Runtime `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:DebugType=None `
    -p:DebugSymbols=false `
    -p:Version=$Version `
    -o $publishDirectory
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed with exit code $LASTEXITCODE" }

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'packaging\Install-Spectator.ps1') -Destination $publishDirectory
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'packaging\Uninstall-Spectator.ps1') -Destination $publishDirectory

$certificateThumbprint = $env:SPECTATOR_SIGN_CERT_SHA1
if (-not [string]::IsNullOrWhiteSpace($certificateThumbprint)) {
    $signTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe'
    if (-not (Test-Path -LiteralPath $signTool)) { throw 'signtool.exe was not found.' }
    & $signTool sign /sha1 $certificateThumbprint /fd SHA256 /td SHA256 /tr 'http://timestamp.digicert.com' (Join-Path $publishDirectory 'Spectator.exe')
    if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
}

$signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $publishDirectory 'Spectator.exe')
$isSigned = $signature.Status -eq [System.Management.Automation.SignatureStatus]::Valid
if (-not [string]::IsNullOrWhiteSpace($certificateThumbprint) -and -not $isSigned) {
    throw "Spectator.exe signature validation failed: $($signature.Status)"
}

$hashFile = Join-Path $publishDirectory 'SHA256SUMS.txt'
$hashLines = Get-ChildItem -LiteralPath $publishDirectory -File |
    Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    Sort-Object Name |
    ForEach-Object { "$(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 | Select-Object -ExpandProperty Hash)  $($_.Name)" }
[System.IO.File]::WriteAllLines($hashFile, $hashLines, [System.Text.UTF8Encoding]::new($false))

$archivePath = Join-Path $artifactsRoot "Spectator-$Version-$Runtime.zip"
Compress-Archive -Path (Join-Path $publishDirectory '*') -DestinationPath $archivePath -CompressionLevel Optimal
$archiveHash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256 | Select-Object -ExpandProperty Hash
if (-not [string]::IsNullOrWhiteSpace($ArchiveUrl) -and -not $ArchiveUrl.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ArchiveUrl must use HTTPS when supplied.'
}
$manifest = [ordered]@{
    schemaVersion = 'spectator.release/v1'
    version = $Version
    runtime = $Runtime
    archiveFile = [System.IO.Path]::GetFileName($archivePath)
    archiveUrl = if ([string]::IsNullOrWhiteSpace($ArchiveUrl)) { $null } else { $ArchiveUrl }
    sha256 = $archiveHash
    signed = $isSigned
    publishedAt = [DateTimeOffset]::UtcNow.ToString('O')
}
$manifestPath = Join-Path $artifactsRoot "Spectator-$Version-$Runtime.manifest.json"
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 4) + [Environment]::NewLine,
    [System.Text.UTF8Encoding]::new($false))
Write-Host "Release package: $archivePath"
Write-Host "Release manifest: $manifestPath"
