<#
.SYNOPSIS
    Applies the HIL migration: removes the two superseded components,
    archives the unused registry pattern, and expects the new
    components/hil/ + Kconfig.projbuild + hal_audio_max98357a.c files
    (from the zip) to already be copied into place first.

.DESCRIPTION
    Run this AFTER extracting hil_migration.zip into your repo root
    (so components/hil/ already exists alongside the old folders).

    What it does:
      1. Archives components/hardwareAccessLayer/ -> deprecated/hardwareAccessLayer/
         (the unused hal_registry.c / hal_driver_ext.h role-registry pattern --
         kept, not deleted, in case it's useful for something dynamic later)
      2. Deletes components/platform_independent_layer/ (superseded by
         components/hil/ -- same working pil_*.c code, just consolidated
         and renamed)
      3. Fixes src/CMakeLists.txt's REQUIRES platform_independent_layer
         -> REQUIRES hil (src/ isn't part of the active PlatformIO build,
         but keeping it internally consistent costs nothing)

    Dry-run by default. Pass -Apply to actually change anything.

.EXAMPLE
    .\apply_hil_migration.ps1 -ProjectPath "D:\...\chronolink-v4-os"
    .\apply_hil_migration.ps1 -ProjectPath "D:\...\chronolink-v4-os" -Apply
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [switch]$Apply
)

$ErrorActionPreference = "Stop"

function Write-Action($text) {
    if ($Apply) { Write-Host "  [DONE]   $text" -ForegroundColor Green }
    else        { Write-Host "  [WOULD]  $text" -ForegroundColor Yellow }
}
function Write-Skip($text) { Write-Host "  [SKIP]   $text" -ForegroundColor DarkGray }

if (-not (Test-Path -LiteralPath $ProjectPath)) {
    Write-Host "ERROR: Project path not found: $ProjectPath" -ForegroundColor Red
    exit 1
}
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path

$hilPath = Join-Path $ProjectPath "components\hil"
if (-not (Test-Path -LiteralPath $hilPath)) {
    Write-Host "ERROR: components\hil\ not found under $ProjectPath" -ForegroundColor Red
    Write-Host "Extract hil_migration.zip into the project root FIRST, then run this script." -ForegroundColor Red
    exit 1
}

Write-Host "HIL migration" -ForegroundColor Magenta
Write-Host "Project path : $ProjectPath"
Write-Host "Mode         : $(if ($Apply) { 'APPLY' } else { 'DRY RUN' })"

# ---------------------------------------------------------------------
# 1. Archive hardwareAccessLayer -> deprecated/
# ---------------------------------------------------------------------
Write-Host ""
Write-Host "== 1. Archive hardwareAccessLayer/ ==" -ForegroundColor Cyan
$hal = Join-Path $ProjectPath "components\hardwareAccessLayer"
$deprecatedDir = Join-Path $ProjectPath "deprecated"
$halDest = Join-Path $deprecatedDir "hardwareAccessLayer"

if (Test-Path -LiteralPath $hal) {
    Write-Action "Move '$hal' -> '$halDest'"
    if ($Apply) {
        New-Item -ItemType Directory -Force -Path $deprecatedDir | Out-Null
        Move-Item -LiteralPath $hal -Destination $halDest -Force
    }
} else {
    Write-Skip "components/hardwareAccessLayer not found -- already moved?"
}

# ---------------------------------------------------------------------
# 2. Delete platform_independent_layer (superseded by hil/)
# ---------------------------------------------------------------------
Write-Host ""
Write-Host "== 2. Remove platform_independent_layer/ (superseded by hil/) ==" -ForegroundColor Cyan
$pil = Join-Path $ProjectPath "components\platform_independent_layer"
if (Test-Path -LiteralPath $pil) {
    Write-Action "Delete '$pil'"
    if ($Apply) { Remove-Item -LiteralPath $pil -Recurse -Force }
} else {
    Write-Skip "components/platform_independent_layer not found -- already removed?"
}

# ---------------------------------------------------------------------
# 3. Fix src/CMakeLists.txt's stale REQUIRES
# ---------------------------------------------------------------------
Write-Host ""
Write-Host "== 3. Fix src/CMakeLists.txt REQUIRES ==" -ForegroundColor Cyan
$srcCMake = Join-Path $ProjectPath "src\CMakeLists.txt"
if (Test-Path -LiteralPath $srcCMake) {
    $content = Get-Content -LiteralPath $srcCMake -Raw
    if ($content -match "REQUIRES\s+platform_independent_layer") {
        Write-Action "Update src/CMakeLists.txt: REQUIRES platform_independent_layer -> REQUIRES hil"
        if ($Apply) {
            $newContent = $content -replace "REQUIRES\s+platform_independent_layer", "REQUIRES hil"
            Set-Content -LiteralPath $srcCMake -Value $newContent -NoNewline
        }
    } else {
        Write-Skip "src/CMakeLists.txt doesn't reference platform_independent_layer -- already fixed?"
    }
} else {
    Write-Skip "src/CMakeLists.txt not found"
}

Write-Host ""
if ($Apply) {
    Write-Host "Migration applied. Build once (idf.py build / pio run) before committing --" -ForegroundColor Green
    Write-Host "this touches Kconfig defaults for two components, worth a real build check." -ForegroundColor Green
} else {
    Write-Host "Dry run only. Re-run with -Apply to make the changes." -ForegroundColor Yellow
}
