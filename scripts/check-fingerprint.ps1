#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
  Compare the installed WHGame.dll's PE fingerprint against the values committed
  in src/builds/steam_offsets.cpp.
.DESCRIPTION
  Same three-field check (TimeDateStamp / SizeOfImage / CheckSum) that
  builds::SelectProfile runs at mod load time. Run this first when a player
  reports the "staying dormant" log line, and after a Steam patch lands, to find
  out whether the RVAs need rederiving before shipping a new mod version.

  It checks Bin\Win64\WHGame.dll, NOT KingdomCome.exe: the exe is a 1.3 MB
  launcher stub, and every address this mod pins is an RVA into WHGame.dll.

  A Steam buildid change is not proof the binaries changed - asset-only patches
  move the buildid and leave the shipping DLL untouched - so "MATCH" after a
  patch is a normal, no-work-needed outcome.

  Exit codes:
    0 = match (no rederivation needed)
    1 = mismatch (rederive + ship a new release)
    2 = could not locate WHGame.dll
.PARAMETER DllPath
  Direct path to WHGame.dll. If omitted, falls back to cameraunlock-core's
  Find-GamePath (env var / Steam manifest / etc).
#>
param(
    [Parameter(Position=0, Mandatory=$false)]
    [string]$DllPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

function Resolve-DllPath {
    param([string]$Provided = '')
    if ($Provided) {
        if (-not (Test-Path $Provided)) {
            throw "WHGame.dll not found at: $Provided"
        }
        return (Resolve-Path $Provided).Path
    }
    $gameRoot = Find-GamePath -GameId 'kingdom-come-deliverance'
    if (-not $gameRoot) {
        throw "Could not locate Kingdom Come: Deliverance. Pass the WHGame.dll path positionally or set `$env:KINGDOM_COME_DELIVERANCE_PATH."
    }
    $dll = Join-Path $gameRoot 'Bin\Win64\WHGame.dll'
    if (-not (Test-Path $dll)) {
        throw "WHGame.dll not found at expected location: $dll"
    }
    return $dll
}

function Read-PEFingerprint {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3c
        $e_lfanew = $reader.ReadUInt32()
        $stream.Position = $e_lfanew
        $sig = $reader.ReadUInt32()
        if ($sig -ne 0x00004550) {
            throw ("Not a PE file: signature 0x{0:x} at e_lfanew=0x{1:x}" -f $sig, $e_lfanew)
        }
        $stream.Position = $e_lfanew + 8
        $tds = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x38
        $size = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x40
        $csum = $reader.ReadUInt32()
        return [pscustomobject]@{
            TimeDateStamp = $tds
            SizeOfImage   = $size
            CheckSum      = $csum
        }
    } finally {
        $stream.Dispose()
    }
}

function Read-ExpectedFingerprints {
    param([string]$ProjectDir)
    # Each profile ships its Fingerprint as a struct initialiser literal:
    #   /* Fingerprint */ { 0x69ccd815u, 0x039eb000u, 0x00000000u },
    # preceded by a /* Name */ "store-platform-YYYYMMDD" line. Collect every
    # profile, mirroring builds::SelectProfile.
    $profiles = @()
    $cppPath = Join-Path $ProjectDir 'src/builds/steam_offsets.cpp'
    if (-not (Test-Path $cppPath)) {
        throw "steam_offsets.cpp not found at $cppPath"
    }
    $cpp = Get-Content -Raw $cppPath
    $pattern = '(?s)Name\s*\*/\s*"([^"]+)".*?Fingerprint\s*\*/\s*\{\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*\}'
    $found = [regex]::Matches($cpp, $pattern)
    if ($found.Count -eq 0) {
        throw "No profile fingerprints found in steam_offsets.cpp"
    }
    foreach ($m in $found) {
        $profiles += [pscustomobject]@{
            Name          = $m.Groups[1].Value
            TimeDateStamp = [Convert]::ToUInt32($m.Groups[2].Value, 16)
            SizeOfImage   = [Convert]::ToUInt32($m.Groups[3].Value, 16)
            CheckSum      = [Convert]::ToUInt32($m.Groups[4].Value, 16)
        }
    }
    return $profiles
}

try {
    $dll = Resolve-DllPath -Provided $DllPath
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 2
}

Write-Host "WHGame.dll: $dll"
$running  = Read-PEFingerprint -Path $dll
$profiles = Read-ExpectedFingerprints -ProjectDir $projectDir

Write-Host ("Running:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}" -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
foreach ($p in $profiles) {
    Write-Host ("Profile:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}  {3}" -f $p.TimeDateStamp, $p.SizeOfImage, $p.CheckSum, $p.Name)
}

$match = $profiles | Where-Object {
    $running.TimeDateStamp -eq $_.TimeDateStamp `
    -and $running.SizeOfImage -eq $_.SizeOfImage `
    -and $running.CheckSum -eq $_.CheckSum
} | Select-Object -First 1

if ($match) {
    Write-Host ("MATCH - profile {0}, no rederivation needed." -f $match.Name) -ForegroundColor Green
    exit 0
}

Write-Host "MISMATCH: WHGame.dll matches no committed profile." -ForegroundColor Yellow
$newest = $profiles | Sort-Object TimeDateStamp -Descending | Select-Object -First 1
if ($running.TimeDateStamp -gt $newest.TimeDateStamp) {
    Write-Host "  The DLL is NEWER than every committed profile - the game was patched."
} elseif ($running.TimeDateStamp -lt $newest.TimeDateStamp) {
    Write-Host "  The DLL is OLDER than the newest profile - let Steam finish updating."
} else {
    Write-Host "  Same build date, different size/checksum - a repacked or modified DLL."
}
Write-Host ""
Write-Host "  Paste-ready profile stub for src/builds/steam_offsets.cpp:"
Write-Host ("    /* Fingerprint */ {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }}," -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
Write-Host ("    /* Name        */ `"steam-win64-{0}`"," -f ([DateTimeOffset]::FromUnixTimeSeconds($running.TimeDateStamp).UtcDateTime.ToString('yyyyMMdd')))
Write-Host ""
Write-Host "  ADD a new build profile - never edit an existing one; the registry is append-only."
exit 1
