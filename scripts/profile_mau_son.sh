#!/usr/bin/env bash
set -euo pipefail

BIN="./build/wuxing"
if [[ ! -x "$BIN" ]]; then
    printf 'Binary not found at %s. Please build first.\n' "$BIN" >&2
    exit 1
fi

WARMUP=55
START_FRAME=40
OUT_DIR="/tmp/mau_son_profile"
mkdir -p "$OUT_DIR"

measure_view() {
    local scale=$1
    local label=$2
    local origin=$3
    local eye=$4
    local out_png="$OUT_DIR/${label}.png"
    local log_file="$OUT_DIR/${label}.log"

    env WUXING_MAP_SCALE="$scale" WUXING_MAP=mau_son RLVK_PROFILE=1 "$BIN" \
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
printf "                       MAU SON MOBA (1x) ALPINE GPU PROFILING                            \n"
printf "=========================================================================================\n"
measure_view "moba" "moba_arena_saddle"   "135.0,3.5,112.5"  "152.0,14.0,132.0"
measure_view "moba" "moba_glacial_tarn"   "160.0,3.5,126.0"  "138.0,16.0,148.0"
measure_view "moba" "moba_upper_cirque"   "208.0,14.0,98.0"  "182.0,26.0,124.0"
measure_view "moba" "moba_ice_arch"       "66.0,4.0,72.0"    "46.0,13.0,52.0"
measure_view "moba" "moba_overview"       "150.0,6.0,112.0"  "220.0,64.0,215.0"

printf "\n=========================================================================================\n"
printf "                 MAU SON OPEN WORLD (4x XZ / 2.2x Y) PROFILING                           \n"
printf "=========================================================================================\n"
measure_view "open_world" "ow_arena_saddle"  "540.0,7.0,450.0"  "584.0,30.0,498.0"
measure_view "open_world" "ow_glacial_tarn"  "640.0,7.0,504.0"  "590.0,32.0,554.0"
printf "=========================================================================================\n\n"
