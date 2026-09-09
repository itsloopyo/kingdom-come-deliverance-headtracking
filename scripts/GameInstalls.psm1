#!/usr/bin/env pwsh
#Requires -Version 5.1
Set-StrictMode -Version Latest

<#
.SYNOPSIS
    Every installed copy of Kingdom Come: Deliverance on this machine, with the
    directory the mod deploys into for each.
.DESCRIPTION
    The stores do not agree on layout. Steam and GOG install
    Bin\Win64\KingdomCome.exe under the game root; the Game Pass package
    installs KingdomCome.exe flat in <XboxGames>\Kingdom Come- Deliverance\
    Content. Ultimate ASI Loader only scans the directory the executable is in,
    so that directory - not the game root - is what every dev task writes to,
    and it is derived from the executable's own relative path rather than
    assumed.

    A machine can hold more than one of these at once, and they are separate
    links of the game needing separate build profiles, so the tasks act on all
    of them rather than on whichever detection returned first.
#>

$Script:CoreModule = Join-Path (Split-Path -Parent $PSScriptRoot) 'cameraunlock-core/powershell/GamePathDetection.psm1'
Import-Module $Script:CoreModule -Force

$Script:GameId = 'kingdom-come-deliverance'

<#
.SYNOPSIS
    Describe one install: where it is, which store it came from, and where the
    ASI and loader go.
.PARAMETER Path
    The game root, as a store's detection reports it.
#>
function New-KcdInstall {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)

    $config = (Get-GameConfigs)[$Script:GameId]

    # The Xbox build's executable relpath is its own field, because the GDK
    # package flattens the Bin\Win64 the other stores keep.
    $isXbox = Test-IsXboxPath -Config $config -Path $Path
    $exeRelPath = if ($isXbox -and $config.ContainsKey('XboxExecutable') -and $config.XboxExecutable) {
        $config.XboxExecutable
    } else {
        $config.Executable
    }

    $store = if ($isXbox) {
        'Game Pass'
    } elseif (@(Find-SteamLibraries) | Where-Object { $Path -like (Join-Path $_ 'steamapps\common\*') }) {
        'Steam'
    } else {
        'install'
    }

    $exePath = Join-Path $Path $exeRelPath
    return [pscustomobject]@{
        Store        = $store
        Path         = $Path
        ExeRelPath   = $exeRelPath
        ExePath      = $exePath
        ExeDirectory = (Split-Path -Parent $exePath)
    }
}

<#
.SYNOPSIS
    Every install on this machine, or just the one at -GamePath when given.
.PARAMETER GamePath
    An explicit game root, trusted verbatim the way install.cmd trusts its first
    argument.
.OUTPUTS
    The objects New-KcdInstall returns, newest-store-agnostic and in detection
    order.
#>
function Get-KcdInstalls {
    [CmdletBinding()]
    param([string]$GamePath = '')

    if ($GamePath) {
        if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
            throw "Game path does not exist or is not a directory: $GamePath"
        }
        return @(New-KcdInstall -Path ((Resolve-Path -LiteralPath $GamePath).Path))
    }

    return @(Find-AllGamePaths -GameId $Script:GameId | ForEach-Object { New-KcdInstall -Path $_ })
}

Export-ModuleMember -Function @('Get-KcdInstalls', 'New-KcdInstall')
