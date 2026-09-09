#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
  Compare every installed copy of the game's WHGame.dll against the profiles
  committed in src/builds/*_offsets.cpp.
.DESCRIPTION
  Same three-field check (TimeDateStamp / SizeOfImage / CheckSum) that
  builds::SelectProfile runs at mod load time. Run this first when a player
  reports the "staying dormant" log line, and after a patch lands, to find out
  whether the RVAs need rederiving before shipping a new mod version.

  Every install found is checked, not just the first: Steam, GOG and Game Pass
  ship separate links of the same game and each needs its own profile. The two
  layouts differ - Steam and GOG put the binaries under Bin\Win64, the Game Pass
  package puts them flat in Content - so the DLL is looked for beside the
  executable rather than at a fixed relative path.

  It checks WHGame.dll, NOT KingdomCome.exe: the exe is a 1.3 MB launcher stub,
  and every address this mod pins is an RVA into WHGame.dll.

  A store's build number changing is not proof the binaries changed - asset-only
  patches move it and leave the shipping DLL untouched - so "MATCH" after a patch
  is a normal, no-work-needed outcome.

  Exit codes:
    0 = every install matched a committed profile
    1 = at least one install matched nothing (rederive + ship a new release)
    2 = no install could be located
.PARAMETER DllPath
  Direct path to a WHGame.dll. If omitted, every install cameraunlock-core can
  find is checked.
#>
param(
    [Parameter(Position=0, Mandatory=$false)]
    [string]$DllPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'GameInstalls.psm1') -Force

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
    # preceded by a /* Name */ "store-platform-YYYYMMDD" line. One file per
    # store, every build of that store inside it, so the whole directory is
    # read rather than a named file - mirroring builds::kKnownProfiles.
    $profiles = @()
    $buildsDir = Join-Path $ProjectDir 'src/builds'
    $sources = @(Get-ChildItem -LiteralPath $buildsDir -Filter '*_offsets.cpp' -File)
    if ($sources.Count -eq 0) {
        throw "No *_offsets.cpp build profiles found in $buildsDir"
    }
    $pattern = '(?s)Name\s*\*/\s*"([^"]+)".*?Fingerprint\s*\*/\s*\{\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*,\s*0x([0-9a-fA-F]+)u?\s*\}'
    foreach ($source in $sources) {
        $cpp = Get-Content -Raw -LiteralPath $source.FullName
        foreach ($m in [regex]::Matches($cpp, $pattern)) {
            $profiles += [pscustomobject]@{
                Name          = $m.Groups[1].Value
                TimeDateStamp = [Convert]::ToUInt32($m.Groups[2].Value, 16)
                SizeOfImage   = [Convert]::ToUInt32($m.Groups[3].Value, 16)
                CheckSum      = [Convert]::ToUInt32($m.Groups[4].Value, 16)
            }
        }
    }
    if ($profiles.Count -eq 0) {
        throw "No profile fingerprints found in $buildsDir"
    }
    return $profiles
}

# Resolve what to check: an explicit DLL, or every install on the machine.
$targets = @()
if ($DllPath) {
    if (-not (Test-Path -LiteralPath $DllPath)) {
        Write-Host "ERROR: WHGame.dll not found at: $DllPath" -ForegroundColor Red
        exit 2
    }
    $targets += [pscustomobject]@{
        Store = 'given path'
        Path  = (Resolve-Path -LiteralPath $DllPath).Path
    }
} else {
    foreach ($install in Get-KcdInstalls) {
        $dll = Join-Path $install.ExeDirectory 'WHGame.dll'
        if (-not (Test-Path -LiteralPath $dll)) {
            Write-Host ("WARNING: {0} install at {1} has no WHGame.dll beside the exe." -f $install.Store, $install.Path) -ForegroundColor Yellow
            continue
        }
        $targets += [pscustomobject]@{ Store = $install.Store; Path = $dll }
    }
}

if ($targets.Count -eq 0) {
    Write-Host "ERROR: Could not locate Kingdom Come: Deliverance. Pass the WHGame.dll path positionally or set `$env:KINGDOM_COME_DELIVERANCE_PATH." -ForegroundColor Red
    exit 2
}

$profiles = Read-ExpectedFingerprints -ProjectDir $projectDir
foreach ($p in $profiles) {
    Write-Host ("Profile:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}  {3}" -f $p.TimeDateStamp, $p.SizeOfImage, $p.CheckSum, $p.Name)
}

$mismatches = 0
foreach ($target in $targets) {
    Write-Host ""
    Write-Host ("{0}: {1}" -f $target.Store, $target.Path)
    $running = Read-PEFingerprint -Path $target.Path
    Write-Host ("Running:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}" -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)

    $match = $profiles | Where-Object {
        $running.TimeDateStamp -eq $_.TimeDateStamp `
        -and $running.SizeOfImage -eq $_.SizeOfImage `
        -and $running.CheckSum -eq $_.CheckSum
    } | Select-Object -First 1

    if ($match) {
        Write-Host ("MATCH - profile {0}, no rederivation needed." -f $match.Name) -ForegroundColor Green
        continue
    }

    $mismatches++
    Write-Host "MISMATCH: this WHGame.dll matches no committed profile." -ForegroundColor Yellow
    $newest = $profiles | Sort-Object TimeDateStamp -Descending | Select-Object -First 1
    if ($running.TimeDateStamp -gt $newest.TimeDateStamp) {
        Write-Host "  The DLL is NEWER than every committed profile - the game was patched."
    } elseif ($running.TimeDateStamp -lt $newest.TimeDateStamp) {
        Write-Host "  The DLL is OLDER than the newest profile - either the store has not finished updating, or this store's build predates the others."
    } else {
        Write-Host "  Same build date, different size/checksum - a repacked or modified DLL."
    }
    # The store decides which src/builds file the stub belongs in, and the
    # profile name carries it, so the two are named together here.
    $slug = switch -Regex ($target.Store) {
        'Game Pass' { 'gdk' }
        'Steam'     { 'steam' }
        'GOG'       { 'gog' }
        default     { 'steam' }
    }
    Write-Host ""
    Write-Host ("  Paste-ready profile stub for src/builds/{0}_offsets.cpp:" -f $slug)
    Write-Host ("    /* Fingerprint */ {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }}," -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
    Write-Host ("    /* Name        */ `"{0}-win64-{1}`"," -f $slug, ([DateTimeOffset]::FromUnixTimeSeconds($running.TimeDateStamp).UtcDateTime.ToString('yyyyMMdd')))
    Write-Host ""
    Write-Host "  ADD a new build profile - never edit an existing one; the registry is append-only."
}

if ($mismatches -gt 0) { exit 1 }
exit 0
