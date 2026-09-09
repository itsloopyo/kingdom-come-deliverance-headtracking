#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built KingdomComeDeliveranceHeadTracking.asi into the game's exe
# directory for local testing.
#
# Usage: deploy.ps1 [GAME_PATH] [-Configuration Debug|Release]
#
# With no GAME_PATH every install on the machine is deployed to - Steam, GOG and
# Game Pass ship separate links of the game, each with its own build profile, so
# a change is worth testing against all of them. Detection order matches
# install.cmd: explicit path -> KINGDOM_COME_DELIVERANCE_PATH env var -> Steam
# registry / library folders -> GOG -> Xbox install roots.

param(
    [Parameter(Position = 0)]
    [string]$GamePath,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Import-Module (Join-Path $PSScriptRoot 'GameInstalls.psm1') -Force

$asi = Join-Path $projectDir "build/$Configuration/KingdomComeDeliveranceHeadTracking.asi"
if (-not (Test-Path $asi)) {
    Write-Error "Build output not found: $asi. Run 'pixi run build' first."
    exit 1
}

$installs = @(Get-KcdInstalls -GamePath $GamePath)
if ($installs.Count -eq 0) {
    Write-Error "Could not locate Kingdom Come: Deliverance. Set KINGDOM_COME_DELIVERANCE_PATH or pass the install path as the first argument."
    exit 1
}

# WHGame.dll - the module that carries the whole engine - imports DINPUT8.dll
# directly in both the Steam and the GDK build, and the application directory is
# searched before System32, so dinput8.dll is the proxy slot on either. Matches
# ASI_LOADER_NAME in install.cmd.
$vendorLoader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'

foreach ($install in $installs) {
    $exeDir = $install.ExeDirectory
    if (-not (Test-Path -LiteralPath $exeDir)) {
        Write-Error "Expected exe directory not found: $exeDir"
        exit 1
    }

    Copy-Item $asi -Destination $exeDir -Force
    Write-Host ("Deployed ({0}): {1} -> {2}" -f $install.Store, (Split-Path -Leaf $asi), $exeDir) -ForegroundColor Green

    $loaderTarget = Join-Path $exeDir 'dinput8.dll'
    if (-not (Test-Path -LiteralPath $loaderTarget)) {
        if (-not (Test-Path $vendorLoader)) {
            Write-Error "Vendored ASI loader missing: $vendorLoader. Run 'pixi run update-deps' and commit the result."
            exit 1
        }
        Copy-Item $vendorLoader -Destination $loaderTarget -Force
        Write-Host "  Installed Ultimate ASI Loader as dinput8.dll" -ForegroundColor Green
    }
}
