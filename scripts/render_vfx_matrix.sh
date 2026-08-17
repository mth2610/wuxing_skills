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
NEWER=$(find core skills sandbox \( -name '*.c' -o -name '*.h' -o -name '*.inl' \) \
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
  env "${VKENV[@]}" WUXING_VFX_BG="$hex" \
      ./build/wuxing --render-vfx 999 --warmup 90 \
                     --out "$OUT/plate_${name}.png" >/dev/null 2>&1 || true
done
echo "  background plates done"

for w in "${WARMUPS[@]}"; do
  for entry in "${BGS[@]}"; do
    name="${entry%%:*}"; hex="${entry##*:}"
    env "${VKENV[@]}" WUXING_VFX_BG="$hex" \
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
