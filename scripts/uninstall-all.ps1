#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
  Run uninstall.cmd against every install of the game on this machine.
.DESCRIPTION
  uninstall.cmd itself takes one game path, because that is the contract the
  launcher calls it through. The dev loop deploys to every install, so removing
  the mod has to cover the same set.

  /y is mandatory on each call: `pixi run` gives the child no TTY, so the body's
  trailing `pause` throws "IOException: The handle is invalid" without it.
.PARAMETER GamePath
  Limit the run to one install.
#>
param(
    [Parameter(Position = 0)]
    [string]$GamePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'GameInstalls.psm1') -Force

$installs = @(Get-KcdInstalls -GamePath $GamePath)
if ($installs.Count -eq 0) {
    Write-Error "Could not locate Kingdom Come: Deliverance. Pass the install path as the first argument."
    exit 1
}

$uninstall = Join-Path $PSScriptRoot 'uninstall.cmd'
$failed = 0
foreach ($install in $installs) {
    Write-Host ("Uninstalling from the {0} install at {1}" -f $install.Store, $install.Path) -ForegroundColor Cyan
    & $uninstall $install.Path '/y'
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("  uninstall.cmd exited {0}" -f $LASTEXITCODE) -ForegroundColor Yellow
        $failed++
    }
}

if ($failed -gt 0) { exit 1 }
exit 0
