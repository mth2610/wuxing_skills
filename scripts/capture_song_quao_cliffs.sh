#!/usr/bin/env bash
# Baseline & verification cliff/cloud captures for Song Quao.
# Usage: bash scripts/capture_song_quao_cliffs.sh [output-directory] [binary]
set -euo pipefail

capture_dir=${1:-/tmp/song_quao_cliff_captures}
capture_binary=${2:-./build/wuxing}
mkdir -p "$capture_dir"

if [[ ! -x "$capture_binary" ]]; then
    printf 'Executable not found: %s\n' "$capture_binary" >&2
    exit 2
fi

git -c core.fsmonitor=false rev-parse HEAD > "$capture_dir/revision.txt" 2>/dev/null || true
git -c core.fsmonitor=false status --short > "$capture_dir/worktree.txt" 2>/dev/null || true
shasum -a 256 "$capture_binary" > "$capture_dir/binary.sha256"

capture() {
    local name=$1 origin=$2 eye=$3
    printf 'Capturing %-18s ... ' "$name"
    local line
    line=$(env WUXING_MAP=song_quao "$capture_binary" \
        --benchmark-visible --profile-start-frame 40 --warmup 60 \
        --render-neutral-smoke --origin "$origin" --eye "$eye" \
        --out "$capture_dir/$name.png" 2>&1 | tee "$capture_dir/$name.log" \
        | grep "BENCHMARK_FRAMETIME" | tail -1 || true)
    
    if [[ ! -s "$capture_dir/$name.png" ]]; then
        printf 'FAILED (no image)\n' >&2
        exit 1
    fi
    if [[ -n "$line" ]]; then
        local avg_ms fps
        avg_ms=$(echo "$line" | sed -E 's/.*avg_ms=([0-9.]+).*/\1/')
        fps=$(echo "$line" | sed -E 's/.*fps=([0-9.]+).*/\1/')
        printf 'OK: %6.2f ms  (%5.1f FPS)\n' "$avg_ms" "$fps"
    else
        printf 'OK\n'
    fi
}

printf "\n=======================================================\n"
printf "        SONG QUAO CLIFF & CLOUD VERIFICATION CAPTURE   \n"
printf "=======================================================\n"
# 1. Close-up over the promontory lip (shows grass bleed, white mist shards, hard cloud seam)
capture "cliff_promontory" "165,-1,240" "178,7,260"
# 2. Looking down the shoreline / river outlet cliff edge
capture "cliff_outlet" "150,0,240" "158,5,255"
# 3. Wide panoramic view from the cloud sea showing the island cliff face
capture "cliff_panorama" "6,-2,150" "-16,7,128"
# 4. Standard gameplay center arena view
capture "gameplay_center" "94,0.5,150" "104,9,162"

printf '\nCaptured images stored in: %s\n' "$capture_dir"
