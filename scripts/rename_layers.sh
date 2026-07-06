#!/usr/bin/env bash
set -euo pipefail

APPLY=false
if [[ "${1:-}" == "--apply" ]]; then APPLY=true; fi

declare -A MAP
MAP["src/pdl"]="src/platformDependentLayer"
MAP["src/pil"]="src/platformIndependentLayer"
MAP["src/hal"]="src/hardwareAccessLayer"

echo "Rename plan:"
for k in "${!MAP[@]}"; do echo "  $k -> ${MAP[$k]}"; done

REPO_ROOT="$(pwd)"
PLANNED_MOVES=()
PLANNED_REPLACES=()

# Collect files to move
for OLD in "${!MAP[@]}"; do
  NEW="${MAP[$OLD]}"
  if [[ ! -d "$OLD" ]]; then
    echo "Note: source folder not found: $OLD"
    continue
  fi
  while IFS= read -r -d '' file; do
    rel="${file#$REPO_ROOT/}"
    dst="${rel/#$OLD/$NEW}"
    PLANNED_MOVES+=("$rel|$dst")
  done < <(find "$OLD" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.S" \) -print0)
done

# Find include replacements
while IFS= read -r -d '' file; do
  content="$(cat "$file")"
  for OLD in "${!MAP[@]}"; do
    NEW="${MAP[$OLD]}"
    if grep -q "$OLD" <<< "$content"; then
      while IFS= read -r line; do
        if [[ "$line" =~ ^[[:space:]]*#.*include.*$OLD ]]; then
          newline="${line//$OLD/$NEW}"
          rel="${file#$REPO_ROOT/}"
          PLANNED_REPLACES+=("$rel|$line|$newline")
        fi
      done < <(grep -n "#.*include.*$OLD" "$file" | sed 's/^[0-9]*://')
    fi
  done
done < <(find "$REPO_ROOT" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0)

echo
echo "Planned file moves:"
for m in "${PLANNED_MOVES[@]}"; do IFS='|' read -r src dst <<< "$m"; echo "  git mv \"$src\" \"$dst\""; done

echo
echo "Planned include replacements:"
for r in "${PLANNED_REPLACES[@]}"; do IFS='|' read -r file old new <<< "$r"; echo "  File: $file"; echo "    Replace: $old"; echo "    With   : $new"; done

if ! $APPLY; then
  echo
  echo "Dry run complete. To apply changes run: $0 --apply"
  exit 0
fi

# Apply changes
for NEWFOLDER in "${MAP[@]}"; do mkdir -p "$NEWFOLDER"; done

for m in "${PLANNED_MOVES[@]}"; do
  IFS='|' read -r src dst <<< "$m"
  dstdir="$(dirname "$dst")"
  mkdir -p "$dstdir"
  git mv -- "$src" "$dst"
  echo "Moved: $src -> $dst"
done

# Apply include replacements
for r in "${PLANNED_REPLACES[@]}"; do
  IFS='|' read -r file old new <<< "$r"
  sed -i "s|$(printf '%s' "$old" | sed 's/[\/&]/\\&/g')|$(printf '%s' "$new" | sed 's/[\/&]/\\&/g')|g" "$file"
  git add "$file"
  echo "Updated includes in: $file"
done

# Add compatibility headers
declare -A COMPAT
COMPAT["src/platformDependentLayer/pdl_compat.h"]=$'/* pdl compatibility */\n#ifndef PDL_COMPAT_H\n#define PDL_COMPAT_H\n#include "platformDependentLayer/pdl_board.h"\ntypedef PlatformDependentLayer_BoardInfo pdl_board_info_t;\nstatic inline const pdl_board_info_t *pdl_board_get_info(void) { return PlatformDependentLayer_get_board_info(); }\nstatic inline void pdl_board_init(void) { PlatformDependentLayer_init(); }\n#endif\n'
COMPAT["src/platformIndependentLayer/pil_compat.h"]=$'/* pil compatibility */\n#ifndef PIL_COMPAT_H\n#define PIL_COMPAT_H\n#include "platformIndependentLayer/pil_config.h"\ntypedef PlatformIndependentLayer_Config pil_config_t;\n#endif\n'
COMPAT["src/hardwareAccessLayer/hal_compat.h"]=$'/* hal compatibility */\n#ifndef HAL_COMPAT_H\n#define HAL_COMPAT_H\n#include "hardwareAccessLayer/hal_gpio.h"\nstatic inline void pil_gpio_init(uint32_t pin, pil_gpio_mode_t mode) { hal_gpio_init(pin, mode); }\n#endif\n'
COMPAT["src/svc/service_engine_alias.h"]=$'/* svc alias */\n#ifndef SERVICE_ENGINE_ALIAS_H\n#define SERVICE_ENGINE_ALIAS_H\n#include "svc/svc_state_manager.h"\ntypedef struct ServiceEngineLayer_State { } ServiceEngineLayer_State;\ntypedef ServiceEngineLayer_State svc_state_t;\n#endif\n'

for path in "${!COMPAT[@]}"; do
  if [[ ! -f "$path" ]]; then
    mkdir -p "$(dirname "$path")"
    printf "%s" "${COMPAT[$path]}" > "$path"
    git add "$path"
    echo "Added: $path"
  else
    echo "Exists: $path"
  fi
done

# Update docs
find docs -type f -name "*.md" -print0 | while IFS= read -r -d '' doc; do
  sed -i 's|src/pdl|src/platformDependentLayer|g; s|src/pil|src/platformIndependentLayer|g; s|src/hal|src/hardwareAccessLayer|g' "$doc"
  git add "$doc"
  echo "Updated docs: $doc"
done

git commit -m "refactor: rename layers (pdl->platformDependentLayer, pil->platformIndependentLayer, hal->hardwareAccessLayer); add compatibility aliases"
git push

echo "Rename and updates applied. Review changes and run your build."
