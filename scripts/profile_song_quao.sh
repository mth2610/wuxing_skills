#!/usr/bin/env bash
set -euo pipefail

BIN="./build/wuxing"
if [[ ! -x "$BIN" ]]; then
    printf 'Binary not found at %s. Please build first.\n' "$BIN" >&2
    exit 1
fi

WARMUP=55
START_FRAME=40
ORIGIN="94,0.5,150"
FAR_EYE="104,9,162"
CLOSE_EYE="95.5,1.3,151.8"
OUT_DIR="/tmp/song_quao_profile"
mkdir -p "$OUT_DIR"

measure_pass() {
    local label=$1
    local skip_env=$2
    local eye=$3
    local out_png="$OUT_DIR/${label}.png"
    
    local line
    line=$(env WUXING_MAP=song_quao WUXING_SONGQUAO_SKIP="$skip_env" "$BIN" \
        --benchmark-visible --profile-start-frame "$START_FRAME" --warmup "$WARMUP" \
        --render-neutral-smoke --origin "$ORIGIN" --eye "$eye" --out "$out_png" 2>&1 \
        | grep "BENCHMARK_FRAMETIME" | tail -1 || true)
    
    if [[ -n "$line" ]]; then
        local avg_ms
        avg_ms=$(echo "$line" | sed -E 's/.*avg_ms=([0-9.]+).*/\1/')
        local fps
        fps=$(echo "$line" | sed -E 's/.*fps=([0-9.]+).*/\1/')
        printf "%-26s : %6.2f ms  (%5.1f FPS)\n" "$label" "$avg_ms" "$fps"
    else
        printf "%-26s : FAILED\n" "$label"
    fi
}

printf "\n=======================================================\n"
printf "       SONG QUAO SUBTRACTIVE GPU PROFILING (FAR VIEW)    \n"
printf "=======================================================\n"
measure_pass "Full Map" "" "$FAR_EYE"
measure_pass "Skip Flora (Grass/Flower)" "flora" "$FAR_EYE"
measure_pass "Skip Ground" "ground" "$FAR_EYE"
measure_pass "Skip Rocks" "rocks" "$FAR_EYE"
measure_pass "Skip Water" "water" "$FAR_EYE"
measure_pass "Skip Sky & CloudSea" "sky,cloudsea" "$FAR_EYE"
measure_pass "Skip Shadow Casters" "shadowcasters" "$FAR_EYE"
measure_pass "Bare Engine (All Skipped)" "ground,rocks,debris,flora,sky,cloudsea,water,mist,shadowcasters" "$FAR_EYE"

printf "\n=======================================================\n"
printf "      SONG QUAO SUBTRACTIVE GPU PROFILING (CLOSE VIEW)   \n"
printf "=======================================================\n"
measure_pass "Full Map (Close)" "" "$CLOSE_EYE"
measure_pass "Skip Flora (Close)" "flora" "$CLOSE_EYE"
measure_pass "Skip Ground (Close)" "ground" "$CLOSE_EYE"
measure_pass "Skip Shadow Casters (Close)" "shadowcasters" "$CLOSE_EYE"

printf "\n=======================================================\n"
printf "      DEFAULT ARENA BASELINE (PostFX + Volumetric Fog)   \n"
printf "=======================================================\n"
arena_line=$(env WUXING_MAP=default_arena "$BIN" \
    --benchmark-visible --profile-start-frame "$START_FRAME" --warmup "$WARMUP" \
    --render-neutral-smoke --origin 6,0,4.4 --eye 16,9,16 --out "$OUT_DIR/arena.png" 2>&1 \
    | grep "BENCHMARK_FRAMETIME" | tail -1 || true)
if [[ -n "$arena_line" ]]; then
    arena_ms=$(echo "$arena_line" | sed -E 's/.*avg_ms=([0-9.]+).*/\1/')
    arena_fps=$(echo "$arena_line" | sed -E 's/.*fps=([0-9.]+).*/\1/')
    printf "%-26s : %6.2f ms  (%5.1f FPS)\n" "Default Arena" "$arena_ms" "$arena_fps"
fi
printf "=======================================================\n\n"
