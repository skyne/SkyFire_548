#!/usr/bin/env bash
set -euo pipefail

CLIENT="${CLIENT:-/home/skyne/Downloads/World of Warcraft 5.4.8}"
OUT="${OUT:-/home/skyne/SkyFire_548/env/dist/data}"
BUILD="${BUILD:-/home/skyne/SkyFire_548/build}"
LOG="${LOG:-$OUT/extract.log}"

mapextractor="$BUILD/src/tools/map_extractor/mapextractor"
vmap4extractor="$BUILD/src/tools/vmap4_extractor/vmap4extractor"
vmap4assembler="$BUILD/src/tools/vmap4_assembler/vmap4assembler"
mmaps_generator="$BUILD/src/tools/mmaps_generator/mmaps_generator"

mkdir -p "$OUT"
cd "$OUT"

{
  echo "=== $(date -Is) client=$CLIENT out=$OUT ==="
  if [[ ! -x "$mapextractor" ]]; then
    echo "missing $mapextractor"
    exit 1
  fi

  if [[ ! -d "$OUT/maps" || ! -d "$OUT/dbc" ]]; then
    echo "=== mapextractor $(date -Is) ==="
    "$mapextractor" -i "$CLIENT" -o "$OUT"
  else
    echo "maps/dbc already present, skipping mapextractor"
  fi
  touch "$OUT/.maps-ready"

  if [[ ! -d "$OUT/vmaps" || ! -f "$OUT/vmaps/000.vmtree" && ! -f "$OUT/vmaps/001.vmtree" ]]; then
    echo "=== vmap4extractor $(date -Is) ==="
    "$vmap4extractor" -d "$CLIENT/Data/"
    echo "=== vmap4assembler $(date -Is) ==="
    "$vmap4assembler" "$OUT/Buildings" "$OUT/vmaps"
  else
    echo "vmaps already present, skipping vmap extract/assemble"
  fi
  touch "$OUT/.vmaps-ready"

  echo "=== mmaps_generator $(date -Is) ==="
  mkdir -p "$OUT/mmaps"
  "$mmaps_generator"
  touch "$OUT/.mmaps-ready"
  echo "=== extract complete $(date -Is) ==="
} 2>&1 | tee -a "$LOG"
