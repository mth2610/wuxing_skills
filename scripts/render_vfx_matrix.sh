#!/usr/bin/env bash
# Render ONE VFX fixture across backgrounds and lifetime phases, at identical framing.
#
# The comparison this exists to make honest: judging "does it hold up on a bright
# background" from two screenshots taken at different zoom levels is not a comparison at
# all — at a smaller apparent size every structure merges, so background and scale are
# confounded and neither can be attributed. Here the camera, the fixture, the warmup
# frame and the resolution are identical across the whole matrix; the ONLY variable is
# the background colour.
#
# Uses machinery that already exists (no rebuild):
#   WUXING_VFX_BG=0xRRGGBBAA   clears the scene to a flat colour AND skips map+skybox
#                              (main.c:1174 — skipping the map is not optional, the
#                              skybox would paint over the clear)
#   --render-vfx <i> --warmup <n> --out <path>   headless, pinned timestep + RNG seed
#   WUXING_TUNING=none         code defaults, NOT the working copy of tuning.cfg
#
# THE TUNING PIN IS NOT OPTIONAL. tuning.cfg persists across sessions and is loaded
# before the headless branch (main.c), so an un-pinned run measures whatever that file
# happens to hold and reports it as a property of the EFFECT. It was found parked
# mid-sweep at bloom_threshold = 0.9 — below 1.0 every diffuse surface blooms itself and
# veils the frame, which costs every effect chroma no matter how it is authored
# (BRIGHT_BACKGROUND_VFX_SPEC.md §7.3). This harness therefore pins to the SHIPPING
# defaults by default, and records what it used in config.txt beside the captures.
# Override with WUXING_TUNING=<path> to measure a specific configuration on purpose.
#
# Backgrounds match BRIGHT_BACKGROUND_VFX_SPEC.md §8.1 so this and the synthetic
# `bright_vfx` chart speak the same language: 0.02 / 0.18 / 1.00 neutral, plus bright
# warm and bright cool.
#
#   scripts/render_vfx_matrix.sh <fixture-name-or-index> [warmup ...]
#   scripts/render_vfx_matrix.sh "FLAME VOLUME" 40 90 140     <- prefer the NAME
#
# Prefer the name: NEWFX indices are positional, so pruning one entry renumbers every
# entry after it and any document citing a number starts pointing somewhere else.
set -euo pipefail
cd "$(dirname "$0")/.."

ARG1="${1:-}"
[ -n "$ARG1" ] || { sed -n '2,24p' "$0"; exit 2; }
shift || true

# Accept a fixture NAME as well as an index, and prefer the name everywhere else.
# NEWFX indices are POSITIONAL: deleting one entry renumbers every entry after it, so a
# document that cites `--render-vfx 38` silently starts pointing at a different effect the
# next time the manifest is pruned. Names do not move.
if printf '%s' "$ARG1" | grep -qE '^[0-9]+$'; then
  IDX="$ARG1"
else
  IDX=$(python3 scripts/vfx_fixture_index.py "$ARG1") || exit 1
  echo "fixture '$ARG1' -> index $IDX"
fi
WARMUPS=("$@"); [ ${#WARMUPS[@]} -gt 0 ] || WARMUPS=(40 90 140)

[ -x ./build/wuxing ] || { echo "build/wuxing not found — build it first"; exit 1; }

# STALE-BINARY GUARD. Shaders are loaded from disk at run time, so a .fs edit takes effect
# with no rebuild — which quietly teaches you that edits "just work". C sources do NOT: a
# change to a .inl/.c is invisible until the binary is rebuilt, and the harness will
# happily measure the old code and report that your fix changed nothing. That exact
# false negative happened on 17/08/2026 with vc_energy_orb.inl. Refuse instead.
# find -newer, not a pipeline: under `set -euo pipefail` an xargs test that returns
# non-zero kills the script before it can print why, which is a guard that fails silently.
# core/tests/ is EXCLUDED: those are standalone headless suites that link nothing from
# the game (scripts/run_core_tests.sh compiles each one on its own), so they can never
# make ./build/wuxing stale. Including them made the guard cry wolf every time a
# regression test was added alongside a fix — and a guard that fires when nothing is
# wrong is one people learn to route around, which is exactly what it exists to prevent.
# main.c IS in this list. It was not, and that is a hole of exactly the kind this guard
# exists to close: main.c owns the frame chain, the headless capture path and the tuning
# init, so a change there is as invisible-until-rebuilt as any .inl — and the guard would
# have waved through a run measuring the previous binary.
NEWER=$(find main.c core skills sandbox \( -name '*.c' -o -name '*.h' -o -name '*.inl' \) \
             -not -path 'core/tests/*' \
             -newer ./build/wuxing 2>/dev/null | head -5 || true)
if [ -n "$NEWER" ]; then
  echo "REFUSING: C sources are newer than ./build/wuxing — the measurement would describe"
  echo "the OLD binary. Rebuild first (cmake --build build -j4), then re-run. Newer files:"
  printf '  %s\n' $NEWER
  echo "  (shader .fs/.vs edits do not need this; they load at run time)"
  exit 1
fi

VSDK="${VULKAN_SDK:-$(ls -d "$HOME"/VulkanSDK/*/macOS 2>/dev/null | sort | tail -1 || true)}"
VKENV=()
[ -n "${VSDK:-}" ] && [ -d "$VSDK" ] && VKENV=(
  VK_ICD_FILENAMES="$VSDK/share/vulkan/icd.d/MoltenVK_icd.json" DYLD_LIBRARY_PATH="$VSDK/lib")

OUT="autotest_output/vfx_matrix/idx$IDX"
rm -rf "$OUT"; mkdir -p "$OUT"

# Pin the live-tuning file, and RECORD the pin next to the captures. A measurement
# whose inputs are not written down cannot be compared with one taken next week.
TUNING="${WUXING_TUNING:-none}"
{
  echo "fixture   : ${ARG1}  (index $IDX)"
  echo "warmups   : ${WARMUPS[*]}"
  echo "tuning    : $TUNING"
  echo "binary    : $(date -r ./build/wuxing '+%Y-%m-%d %H:%M:%S')"
  echo "git HEAD  : $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "git dirty : $(git status --porcelain 2>/dev/null | wc -l | tr -d ' ') file(s)"
  if [ "$TUNING" != "none" ] && [ -f "$TUNING" ]; then
    echo "--- $TUNING ---"; grep -vE '^\s*(#|$)' "$TUNING" || true
  fi
} > "$OUT/config.txt"
echo "  tuning pinned to '$TUNING' (recorded in $OUT/config.txt)"

# name:hex  — hex is RGBA; the 8-bit value lands in the HDR target as value/255 linear
BGS=(dark:0x050505FF mid:0x2E2E2EFF white:0xFFFFFFFF warm:0xFFB859FF cool:0x59B8FFFF)

# A BACKGROUND PLATE per colour: the same background rendered with no fixture
# (index 999 fires nothing). Without it there is no honest reference — the first
# version of this harness derived the background from a radial median of the frame
# itself, which the BACKGROUND'S OWN BLOOM breaks: on a bright backdrop the veil near
# the effect lifts the median, and the mask then counted 39% of the frame as "effect"
# when the true footprint is 6%. Every conclusion drawn from that would have been
# about the measurement, not the effect.
for entry in "${BGS[@]}"; do
  name="${entry%%:*}"; hex="${entry##*:}"
  env "${VKENV[@]}" WUXING_TUNING="$TUNING" WUXING_VFX_BG="$hex" \
      ./build/wuxing --render-vfx 999 --warmup 90 \
                     --out "$OUT/plate_${name}.png" >/dev/null 2>&1 || true
done
echo "  background plates done"

for w in "${WARMUPS[@]}"; do
  for entry in "${BGS[@]}"; do
    name="${entry%%:*}"; hex="${entry##*:}"
    env "${VKENV[@]}" WUXING_TUNING="$TUNING" WUXING_VFX_BG="$hex" \
        ./build/wuxing --render-vfx "$IDX" --warmup "$w" \
                       --out "$OUT/${name}_w${w}.png" >/dev/null 2>&1 || true
    [ -f "$OUT/${name}_w${w}.png" ] || echo "  MISSING ${name}_w${w}"
  done
  echo "  warmup $w done"
done

echo "matrix in $OUT/"
PY=python3
for cand in python3 /usr/bin/python3; do
  "$cand" -c "import numpy, PIL" >/dev/null 2>&1 && { PY="$cand"; break; }
done
"$PY" scripts/analyze_vfx_matrix.py "$OUT"
