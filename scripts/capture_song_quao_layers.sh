#!/usr/bin/env bash
# Fixed-camera capture set for the Song Quao visual upgrade.
# Usage: bash scripts/capture_song_quao_layers.sh [output-directory] [binary]
# Quality tier is inherited from WUXING_GFX_QUALITY (default: platform default).
set -euo pipefail

capture_dir=${1:-/tmp/song_quao_layers}
capture_binary=${2:-./build/wuxing}
mkdir -p "$capture_dir"

if [[ ! -x "$capture_binary" ]]; then
    printf 'Executable not found: %s\n' "$capture_binary" >&2
    exit 2
fi

git -c core.fsmonitor=false rev-parse HEAD > "$capture_dir/revision.txt" 2>/dev/null || true
git -c core.fsmonitor=false status --short > "$capture_dir/worktree.txt" 2>/dev/null || true
shasum -a 256 "$capture_binary" > "$capture_dir/binary.sha256"
printf 'quality=%s\n' "${WUXING_GFX_QUALITY:-platform-default}" > "$capture_dir/quality.txt"

capture() {
    local name=$1 origin=$2 eye=$3
    printf 'Capturing %-18s ... ' "$name"
    local line
    line=$(env WUXING_MAP=song_quao "$capture_binary" \
        --benchmark-visible --profile-start-frame 40 --warmup 60 \
        --render-vfx -1 --origin "$origin" --eye "$eye" \
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
printf "          SONG QUAO FOUR-LAYER VERIFICATION SET       \n"
printf "=======================================================\n"
# 1. Hero bank: full 3/4 view across the river bend, ~23 m back and ~14 m up.
capture "hero_bank" "92,0.5,149" "108,14,165"
# 2. Actual gameplay orbit camera (also used by the cliff script).
capture "gameplay_center" "94,0.5,150" "104,9,162"
# 3. High overview from the south-west, framing the whole island and river.
capture "overview_ridge" "94,2,150" "20,78,30"
# 4. Wide cliff/cloud-sea panorama.
capture "cliff_panorama" "6,-2,150" "-16,7,128"

printf '\nCaptured images stored in: %s\n' "$capture_dir"
