#!/usr/bin/env bash
# Repeatable Phase-0 captures. Run from the repository root after building.
# Usage: bash scripts/capture_meadow_fixtures.sh [output-directory] [binary]
set -euo pipefail

capture_dir=${1:-$(mktemp -d /tmp/wuxing-meadow-XXXXXX)}
capture_binary=${2:-./build/wuxing}
mkdir -p "$capture_dir"
if [[ ! -x "$capture_binary" ]]; then
    printf 'Executable not found: %s\n' "$capture_binary" >&2
    exit 2
fi

# Preserve the exact source delta and tuning identity alongside the images.
git -c core.fsmonitor=false rev-parse HEAD > "$capture_dir/revision.txt"
git -c core.fsmonitor=false diff --binary > "$capture_dir/source.patch"
git -c core.fsmonitor=false status --short > "$capture_dir/worktree.txt"
shasum -a 256 "$capture_binary" > "$capture_dir/binary.sha256"
if [[ -f "${WUXING_TUNING:-tuning.cfg}" ]]; then
    shasum -a 256 "${WUXING_TUNING:-tuning.cfg}" > "$capture_dir/tuning.sha256"
fi

capture() {
    local name=$1 origin=$2 eye=$3
    env WUXING_MAP=verdant_path "$capture_binary" \
        --render-neutral-smoke --origin "$origin" --eye "$eye" \
        --warmup 90 --out "$capture_dir/$name.png" \
        > "$capture_dir/$name.log" 2>&1
    if [[ ! -s "$capture_dir/$name.png" ]]; then
        printf 'Capture failed: %s (see log)\n' "$name" >&2
        exit 1
    fi
    if rg -i 'shader.*(failed|error)|failed.*shader' "$capture_dir/$name.log"; then
        printf 'Shader error in %s\n' "$name" >&2
        exit 1
    fi
    rg 'CAPTURE:|WUXING_MAP:|Device:|HDR float|Render size:' "$capture_dir/$name.log"
}

capture flowers 27,0,20 30,5,25
capture overview 29,0,54 35,12,65
capture shore 52,0,25 48,4,30
capture lake 63,0,25.5 66,9,37
printf 'Capture evidence: %s\n' "$capture_dir"
