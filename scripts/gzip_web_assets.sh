#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ -x "scripts/generate_cfgdoc_chunks.py" ]]; then
  scripts/generate_cfgdoc_chunks.py
fi

assets=(
  "data/webinterface/index.html"
  "data/webinterface/sh.html"
  "data/webinterface/app.js"
  "data/webinterface/app-core.css"
  "data/webinterface/app-core.js"
  "data/webinterface/light.html"
  "data/webinterface/light.css"
  "data/webinterface/light.js"
  "data/webinterface/runtimeui.json"
)

for asset in "${assets[@]}"; do
  if [[ -f "$asset" ]]; then
    gzip -n -9 -c "$asset" > "$asset.gz"
  fi
done

if [[ -f "data/webinterface/cfgdocs.fr.json" ]]; then
  gzip -n -9 -c "data/webinterface/cfgdocs.fr.json" > "data/webinterface/cfgdocs.jz"
fi

if [[ -f "data/webinterface/cfgmods.fr.json" ]]; then
  gzip -n -9 -c "data/webinterface/cfgmods.fr.json" > "data/webinterface/cfgmods.jz"
fi

if [[ -d "data/wc" ]]; then
  while IFS= read -r -d '' file; do
    gzip -n -9 -c "$file" > "${file}.gz"
  done < <(find "data/wc" -type f -name '*.j' -print0)
fi
