#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built KingdomComeDeliveranceHeadTracking.asi into the game's exe
# directory for local testing.
#
# Usage: deploy.ps1 [GAME_PATH] [-Configuration Debug|Release]
# Game detection order matches install.cmd: explicit path ->
# KINGDOM_COME_DELIVERANCE_PATH env var -> Steam registry / library folders ->
# games.json.

param(
    [Parameter(Position = 0)]
    [string]$GamePath,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$asi = Join-Path $projectDir "build/$Configuration/KingdomComeDeliveranceHeadTracking.asi"
if (-not (Test-Path $asi)) {
    Write-Error "Build output not found: $asi. Run 'pixi run build' first."
    exit 1
}

if (-not $GamePath) {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'kingdom-come-deliverance'
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    Write-Error "Could not locate Kingdom Come: Deliverance. Set KINGDOM_COME_DELIVERANCE_PATH or pass the install path as the first argument."
    exit 1
}

$exeDir = Join-Path $GamePath 'Bin\Win64'
if (-not (Test-Path $exeDir)) {
    Write-Error "Expected exe directory not found: $exeDir"
    exit 1
}

Copy-Item $asi -Destination $exeDir -Force
Write-Host "Deployed: $asi -> $exeDir" -ForegroundColor Green

# WHGame.dll - the module that carries the whole engine - imports DINPUT8.dll
# directly, and the application directory is searched before System32, so
# dinput8.dll is the proxy slot. Matches ASI_LOADER_NAME in install.cmd.
$loaderTarget = Join-Path $exeDir 'dinput8.dll'
if (-not (Test-Path $loaderTarget)) {
    $vendorLoader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path $vendorLoader)) {
        Write-Error "Vendored ASI loader missing: $vendorLoader. Run 'pixi run update-deps' and commit the result."
        exit 1
    }
    Copy-Item $vendorLoader -Destination $loaderTarget -Force
    Write-Host "Installed Ultimate ASI Loader as dinput8.dll" -ForegroundColor Green
}
