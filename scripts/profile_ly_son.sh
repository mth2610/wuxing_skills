#!/usr/bin/env bash
set -euo pipefail

BIN="./build/wuxing"
if [[ ! -x "$BIN" ]]; then
    printf 'Binary not found at %s. Please build first.\n' "$BIN" >&2
    exit 1
fi

WARMUP=55
START_FRAME=40
OUT_DIR="/tmp/ly_son_profile"
mkdir -p "$OUT_DIR"

measure_view() {
    local scale=$1
    local label=$2
    local origin=$3
    local eye=$4
    local out_png="$OUT_DIR/${label}.png"
    local log_file="$OUT_DIR/${label}.log"

    env WUXING_MAP_SCALE="$scale" WUXING_MAP=ly_son RLVK_PROFILE=1 "$BIN" \
        --benchmark-visible --profile-start-frame "$START_FRAME" --warmup "$WARMUP" \
        --render-neutral-smoke --origin "$origin" --eye "$eye" --out "$out_png" \
        >"$log_file" 2>&1 || true

    local line
    line=$(grep "BENCHMARK_FRAMETIME" "$log_file" | tail -1 || true)
    local prof
    prof=$(grep "RLVK_PROFILE" "$log_file" | tail -1 || true)
    local dd_warns
    dd_warns=$(grep -c "deferred-destruction queue full" "$log_file" || true)

    if [[ -n "$line" ]]; then
        local avg_ms fps
        avg_ms=$(echo "$line" | sed -E 's/.*avg_ms=([0-9.]+).*/\1/')
        fps=$(echo "$line" | sed -E 's/.*fps=([0-9.]+).*/\1/')
        local extra=""
        if [[ -n "$prof" ]]; then
            extra=" | $(echo "$prof" | sed -E 's/.*RLVK_PROFILE[^:]*: //')"
        fi
        printf "%-24s : %6.2f ms (%5.1f FPS) | dd_warn=%s%s\n" \
            "$label" "$avg_ms" "$fps" "$dd_warns" "$extra"
    else
        printf "%-24s : FAILED (see %s)\n" "$label" "$log_file"
    fi
}

printf "\n=========================================================================================\n"
printf "                       LY SON MOBA (1x) VOLCANIC GPU PROFILING                           \n"
printf "=========================================================================================\n"
measure_view "moba" "moba_arena_saddle"   "144.0,3.0,105.0"  "162.0,13.0,124.0"
measure_view "moba" "moba_caldera_magma"  "212.0,14.0,96.0"  "186.0,26.0,122.0"
measure_view "moba" "moba_lava_channel"   "176.0,5.0,128.0"  "195.0,15.0,148.0"
measure_view "moba" "moba_coast_tovo"     "68.0,2.5,62.0"    "44.0,12.0,42.0"
measure_view "moba" "moba_overview"       "150.0,6.0,105.0"  "220.0,62.0,210.0"

printf "\n=========================================================================================\n"
printf "                  LY SON OPEN WORLD (4x XZ / 2.2x Y) PROFILING                           \n"
printf "=========================================================================================\n"
measure_view "open_world" "ow_arena_saddle"  "576.0,6.0,420.0"  "620.0,28.0,470.0"
measure_view "open_world" "ow_caldera_magma" "848.0,26.0,384.0" "760.0,52.0,460.0"
printf "=========================================================================================\n\n"
