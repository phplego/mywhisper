#!/usr/bin/env bash
set -euo pipefail
base_file=".loc-baseline"
if [[ ! -f "$base_file" ]]; then
  echo "missing $base_file" >&2
  exit 2
fi
base=$(cat "$base_file")
current=$(find . -maxdepth 2 -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -not -path './.git/*' -print0 | xargs -0 sed '/^[[:space:]]*\/\//d' | wc -l)
printf 'LOC baseline=%s current=%s\n' "$base" "$current"
if (( current > base )); then
  echo "LOC increased" >&2
  exit 1
fi
