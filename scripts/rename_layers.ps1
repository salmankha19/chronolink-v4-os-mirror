<#
Rename layer folders and update includes (PowerShell).
Usage:
  # Dry run (default)
  .\scripts\rename_layers.ps1

  # Apply changes
  .\scripts\rename_layers.ps1 -Apply
#>

param(
    [switch]$Apply
)

# Mapping: old -> new
$map = @{
    "src/pdl" = "src/platformDependentLayer"
    "src/pil" = "src/platformIndependentLayer"
    "src/hal" = "src/hardwareAccessLayer"
}

Write-Host "Rename plan:" -ForegroundColor Cyan
foreach ($k in $map.Keys) { Write-Host "  $k -> $($map[$k])" }

$repoRoot = (Get-Location).Path

# Collect planned moves
$plannedMoves = @()
foreach ($old in $map.Keys) {
    $new = $map[$old]
    if (-Not (Test-Path $old)) {
        Write-Host "Note: source folder not found: $old" -ForegroundColor Yellow
        continue
    }
    $files = Get-ChildItem -Path $old -Recurse -File | Where-Object { $_.Extension -in ".c",".h",".cpp",".hpp",".S" }
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($repoRoot.Length+1) -replace '\\','/'
        $dest = $rel -replace "^$old",$new
        $plannedMoves += @{ src = $rel; dst = $dest }
    }
}

# Collect planned include replacements
$plannedReplaces = @()
$sourceFiles = Get-ChildItem -Path $repoRoot -Recurse -File -Include *.c,*.h,*.cpp,*.hpp
foreach ($old in $map.Keys) {
    $new = $map[$old]
    $escapedOld = [regex]::Escape($old)
    foreach ($file in $sourceFiles) {
        $lines = Get-Content $file.FullName
        for ($i = 0; $i -lt $lines.Length; $i++) {
            $line = $lines[$i]
            # Build a regex that matches include lines containing the old path
            $regex = '#\s*include\s*[\"<].*' + $escapedOld
            if ($line -match $regex) {
                $oldLine = $line.Trim()
                $newLine = $oldLine -replace $escapedOld, $new
                $plannedReplaces += @{ file = $file.FullName.Substring($repoRoot.Length+1) -replace '\\','/'; old = $oldLine; new = $newLine }
            }
        }
    }
}

Write-Host "`nPlanned file moves:" -ForegroundColor Cyan
foreach ($m in $plannedMoves) { Write-Host "  git mv `"$($m.src)`" `"$($m.dst)`"" }

Write-Host "`nPlanned include replacements:" -ForegroundColor Cyan
foreach ($r in $plannedReplaces) {
    Write-Host "  File: $($r.file)"
    Write-Host "    Replace: $($r.old)"
    Write-Host "    With   : $($r.new)"
}

if (-Not $Apply) {
    Write-Host "`nDry run complete. To apply changes re-run with -Apply." -ForegroundColor Green
    exit 0
}

# === Apply changes ===
Write-Host "`nApplying changes..." -ForegroundColor Cyan

# Create new folders
foreach ($old in $map.Keys) {
    $new = $map[$old]
    if (-Not (Test-Path $new)) {
        New-Item -ItemType Directory -Path $new -Force | Out-Null
        Write-Host "Created: $new"
    }
}

# Perform git mv for each planned move
foreach ($m in $plannedMoves) {
    $src = $m.src
    $dst = $m.dst
    $dstDir = Split-Path $dst -Parent
    if (-Not (Test-Path $dstDir)) {
        New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
    }
    git mv -- "$src" "$dst"
    Write-Host "Moved: $src -> $dst"
}

# Update includes in all source files
foreach ($r in $plannedReplaces) {
    $file = Join-Path $repoRoot $r.file
    $escapedOld = [regex]::Escape($r.old)
    # Use -replace with escaped old text (literal replacement)
    $content = Get-Content $file -Raw
    $newContent = $content -replace [regex]::Escape($r.old), [regex]::Escape($r.new) -replace '\\\\','\'  # ensure backslashes normalized
    # Simpler: do a literal string replace using .Replace to avoid regex pitfalls
    $newContent = $content.Replace($r.old, $r.new)
    Set-Content -Path $file -Value $newContent
    git add $r.file
    Write-Host "Updated includes in: $($r.file)"
}

# Add compatibility alias headers (only if they don't already exist)
$compatPdlPath = "src/platformDependentLayer/pdl_compat.h"
$compatPilPath = "src/platformIndependentLayer/pil_compat.h"
$compatHalPath = "src/hardwareAccessLayer/hal_compat.h"
$svcAliasPath  = "src/svc/service_engine_alias.h"

$compatPdlContent = @'
/* Compatibility aliases: expose PlatformDependentLayer public types while keeping pdl_* aliases */
#ifndef PDL_COMPAT_H
#define PDL_COMPAT_H

#include "platformDependentLayer/pdl_board.h"

/* Public descriptive typedef */
typedef PlatformDependentLayer_BoardInfo pdl_board_info_t;

/* Inline wrappers */
static inline const pdl_board_info_t *pdl_board_get_info(void) { return PlatformDependentLayer_get_board_info(); }
static inline void pdl_board_init(void) { PlatformDependentLayer_init(); }

#endif /* PDL_COMPAT_H */
'@

$compatPilContent = @'
/* Compatibility aliases for platformIndependentLayer (pil) */
#ifndef PIL_COMPAT_H
#define PIL_COMPAT_H

#include "platformIndependentLayer/pil_config.h"

/* Keep short aliases if needed (no-op typedefs) */
/* typedef PlatformIndependentLayer_Config pil_config_t;  -- define if you add that type */

#endif /* PIL_COMPAT_H */
'@

$compatHalContent = @'
/* Compatibility aliases for hardwareAccessLayer (hal) */
#ifndef HAL_COMPAT_H
#define HAL_COMPAT_H

#include "hardwareAccessLayer/hal_gpio.h"

/* Example alias: keep old pil_gpio symbols available by forwarding to HAL implementations */
/* static inline void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode) { hal_gpio_init(pin, mode); } */

#endif /* HAL_COMPAT_H */
'@

$svcAliasContent = @'
/* ServiceEngineLayer alias header for docs and public API */
#ifndef SERVICE_ENGINE_ALIAS_H
#define SERVICE_ENGINE_ALIAS_H

#include "svc/svc_state_manager.h"

/* Descriptive public name placeholder */
typedef struct ServiceEngineLayer_State { /* placeholder */ } ServiceEngineLayer_State;

/* Keep svc_* aliases */
typedef ServiceEngineLayer_State svc_state_t;

#endif /* SERVICE_ENGINE_ALIAS_H */
'@

if (-Not (Test-Path $compatPdlPath)) { $compatPdlContent | Out-File -FilePath $compatPdlPath -Encoding utf8; git add $compatPdlPath; Write-Host "Added: $compatPdlPath" } else { Write-Host "Exists: $compatPdlPath" }
if (-Not (Test-Path $compatPilPath)) { $compatPilContent | Out-File -FilePath $compatPilPath -Encoding utf8; git add $compatPilPath; Write-Host "Added: $compatPilPath" } else { Write-Host "Exists: $compatPilPath" }
if (-Not (Test-Path $compatHalPath)) { $compatHalContent | Out-File -FilePath $compatHalPath -Encoding utf8; git add $compatHalPath; Write-Host "Added: $compatHalPath" } else { Write-Host "Exists: $compatHalPath" }
if (-Not (Test-Path $svcAliasPath))  { $svcAliasContent  | Out-File -FilePath $svcAliasPath  -Encoding utf8; git add $svcAliasPath;  Write-Host "Added: $svcAliasPath"  } else { Write-Host "Exists: $svcAliasPath" }

# Update docs: replace occurrences in docs folder
Get-ChildItem -Path docs -Recurse -File -Include *.md | ForEach-Object {
    $path = $_.FullName
    $text = Get-Content $path -Raw
    $text = $text -replace 'src/pdl','src/platformDependentLayer'
    $text = $text -replace 'src/pil','src/platformIndependentLayer'
    $text = $text -replace 'src/hal','src/hardwareAccessLayer'
    Set-Content -Path $path -Value $text
    git add $path
    Write-Host "Updated docs: $path"
}

# Final commit
git commit -m "refactor: rename layers (pdl->platformDependentLayer, pil->platformIndependentLayer, hal->hardwareAccessLayer); add compatibility aliases"
git push

Write-Host "`nRename and updates applied. Review changes and run your build." -ForegroundColor Green
