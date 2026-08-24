#!/usr/bin/env pwsh
#Requires -Version 5.1
# Packaging for Kingdom Come: Deliverance Head Tracking (C++ / CMake, no .csproj).
# Produces two ZIPs in release/:
#   - KingdomComeDeliveranceHeadTracking-v{version}-installer.zip (GitHub Release)
#   - KingdomComeDeliveranceHeadTracking-v{version}-nexus.zip     (Nexus, extract to game folder)
#
# The vendored loader is consumed exactly as committed - refreshing it is
# `pixi run update-deps`, a deliberate dev action with a commit attached.
# Zero prompts, zero network.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modName    = 'KingdomComeDeliveranceHeadTracking'

Import-Module (Join-Path $projectDir 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

# Both ZIPs are binary distributions: MIT (our code, cameraunlock-core, the
# loader) and BSD-2-Clause (MinHook) all require their notices to travel with the
# binary, so a missing file fails the build rather than shipping a ZIP that
# quietly satisfies none of them.
function Copy-RequiredNotices {
    param([string[]] $Names, [string] $Destination)
    foreach ($name in $Names) {
        $src = Join-Path $projectDir $name
        if (-not (Test-Path $src)) {
            throw "Required notice file not found: $name. Every published ZIP is a binary distribution and must carry it."
        }
        Copy-Item -Path $src -Destination $Destination -Force
        Write-Host "  $name" -ForegroundColor Green
    }
}

$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch "project\($modName VERSION (\d+\.\d+\.\d+)") {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]

Write-Host ''
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ''

$asiPath = Join-Path $projectDir "build\Release\$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build' first."
}

$vendorAsiDir = Join-Path $projectDir 'vendor\ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDll)) {
    throw "Bundled ASI loader missing: $vendorAsiDll. Run 'pixi run update-deps' and commit the result."
}

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) { throw "Required script not found: $s" }
}

$launcherManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

# --- Installer ZIP -----------------------------------------------------
Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$ghStaging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $ghStaging -Force
}

# Shared detection bundle (find-game.ps1 + GamePathDetection.psm1 + games.json
# + the install/uninstall bodies). install.cmd resolves the game through
# shared\find-game.ps1 on every run, and find-game.ps1 in turn needs
# GamePathDetection.psm1 beside it - hand-copying only find-game.ps1 produces a
# ZIP that hard-errors for every user.
Copy-SharedBundle -StagingDir $ghStaging

$pluginsDir = Join-Path $ghStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force

$ghVendorDir = Join-Path $ghStaging 'vendor\ultimate-asi-loader'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
foreach ($vendorFile in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (-not (Test-Path $src)) { throw "Vendored loader file missing: $src. Run 'pixi run update-deps'." }
    Copy-Item $src -Destination $ghVendorDir -Force
}

Copy-RequiredNotices @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md') $ghStaging

# Stamp mod_info.version from the build so the shipped manifest can never
# disagree with the built .asi. Targeted replace keeps the hand-authored
# layout (mod_info.version is the only semver in the file).
$manifestText = Get-Content $launcherManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText((Join-Path $ghStaging 'launcher-manifest.json'), $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $ghStaging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $ghStaging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green

# --- Nexus ZIP ---------------------------------------------------------
Write-Host ''
Write-Host '--- Nexus ZIP ---' -ForegroundColor Yellow

# Nexus users manage their own ASI loader, so this ships the mod's .asi under
# the deploy subtree plus the notices below - no vendored loader, no scripts.
$nexusStaging = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }
$nexusGameDir = Join-Path $nexusStaging 'Bin\Win64'
New-Item -ItemType Directory -Path $nexusGameDir -Force | Out-Null
Copy-Item $asiPath -Destination $nexusGameDir -Force

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
Copy-RequiredNotices @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md') $nexusStaging
Push-Location $nexusStaging
try { Compress-Archive -Path '.\*' -DestinationPath $nexusZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStaging

$nexusKb = [math]::Round((Get-Item $nexusZip).Length / 1KB, 1)
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKb) -ForegroundColor Green

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
Write-Output $nexusZip
