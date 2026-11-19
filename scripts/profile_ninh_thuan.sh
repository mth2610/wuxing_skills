#!/usr/bin/env bash
set -euo pipefail

BIN="./build/wuxing"
if [[ ! -x "$BIN" ]]; then
    printf 'Binary not found at %s. Please build first.\n' "$BIN" >&2
    exit 1
fi

WARMUP=55
START_FRAME=40
OUT_DIR="/tmp/ninh_thuan_profile"
mkdir -p "$OUT_DIR"

measure_view() {
    local scale=$1
    local label=$2
    local origin=$3
    local eye=$4
    local out_png="$OUT_DIR/${label}.png"
    local log_file="$OUT_DIR/${label}.log"

    env WUXING_MAP_SCALE="$scale" WUXING_MAP=ninh_thuan RLVK_PROFILE=1 "$BIN" \
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
printf "                     NINH THUAN MOBA (1x) VISUAL & GPU PROFILING                         \n"
printf "=========================================================================================\n"
measure_view "moba" "moba_spawn_oasis"    "169.7,1.5,230.0" "150.0,10.0,248.0"
measure_view "moba" "moba_canyon_kopje"   "94.1,2.0,150.0"  "112.0,11.0,168.0"
measure_view "moba" "moba_arena_gameplay" "94.1,1.0,150.0"  "104.0,9.5,162.0"
measure_view "moba" "moba_coast_surf"     "178.0,0.0,150.0" "152.0,12.0,132.0"
measure_view "moba" "moba_overview"       "94.1,2.0,150.0"  "165.0,55.0,275.0"
measure_view "moba" "cliff_closeup"       "182.0,-1.0,210.0" "210.0,7.0,215.0"

printf "\n=========================================================================================\n"
printf "                  NINH THUAN OPEN WORLD (4x XZ / 2.2x Y) PROFILING                       \n"
printf "=========================================================================================\n"
measure_view "open_world" "ow_oasis_sanctuary" "678.8,3.3,920.0" "642.0,22.0,956.0"
measure_view "open_world" "ow_dunes_kopje"     "540.0,4.0,780.0" "572.0,24.0,818.0"
measure_view "open_world" "ow_canyon_vista"    "376.4,4.0,600.0" "416.0,28.0,645.0"
measure_view "open_world" "ow_coast_surf"      "712.0,-2.0,600.0" "655.0,28.0,550.0"
measure_view "open_world" "ow_high_overview"   "376.4,5.0,600.0" "620.0,155.0,1050.0"
printf "=========================================================================================\n\n"
