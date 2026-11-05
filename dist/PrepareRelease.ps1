# Run from any directory:
# powershell -NoProfile -ExecutionPolicy Bypass -File .\PrepareRelease.ps1 -Version 13.0.2
# Creates a public portable ZIP; NEVER includes data/, tdata/, or sessions.
param([Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must look like 13.0.2' }
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($name in @('Customgram.exe','Updater.exe','github-repo.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)) {
        throw "Missing $name in $root"
    }
}
$out = Join-Path $root ('release-' + $Version)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$stage = Join-Path $out '_stage'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    foreach ($name in @('Customgram.exe','Updater.exe','github-repo.txt')) {
        Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $stage $name)
    }
    Set-Content -LiteralPath (Join-Path $stage 'version.txt') -Value $Version -Encoding ascii
    $modules = Join-Path $root 'modules'
    if (Test-Path -LiteralPath $modules -PathType Container) {
        Copy-Item -LiteralPath $modules -Destination (Join-Path $stage 'modules') -Recurse
    }
    $zip = Join-Path $out 'Customgram-Windows.zip'
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath (Join-Path $out 'Customgram-Windows.zip.sha256') -Value "$hash  Customgram-Windows.zip" -Encoding ascii
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Host "Ready: $zip"
Write-Host "Upload the ZIP and SHA256 to the GitHub release v$Version"
Write-Host 'NEVER publish data/, tdata/, or login sessions.'
