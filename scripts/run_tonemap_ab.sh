#!/usr/bin/env bash
# Gate 2 of the hue-preserving tone-map approval.
#   third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md §12.1
#
# Captures the SAME scene twice — once on the shipping ACES curve, once on the
# candidate — and reports how much of the frame actually moved.
#
# Why the number means something: `tonemap_shoulder` proves the candidate is the exact
# identity below exposed peak 1.0, so every differing pixel is a pixel that was already
# above the shoulder. The "changed %" below IS the approval surface — if it is small and
# confined to emissive areas, this is not a whole-scene change and does not need a
# whole-scene re-approval.
#
# It runs A twice FIRST to establish the noise floor. Without that, an A/B number is
# uninterpretable: headless capture pins the timestep and the RNG seed, but nothing
# proves it stayed pinned, and a probe you have not validated is not evidence.
#
# Usage:
#   scripts/run_tonemap_ab.sh --verify <skill>   [strength]   # whole scene + a real skill
#   scripts/run_tonemap_ab.sh --vfx    <index>   [strength]   # one NEWFX fixture
# strength defaults to 0.6.
#
# <skill> is a registered skill NAME (core/skill_manager.c):
#   WATER  TUBE  METAL  FIRE  WOOD  ELECTRIC  WIND  SHIELD
# FIRE is the best first probe — a warm HDR core over the night arena is exactly the
# content that sits in the shoulder band the candidate curve touches.
set -euo pipefail
cd "$(dirname "$0")/.."

# Prefer an interpreter that has numpy/Pillow: the diff falls back to pure stdlib, but
# the fallback is a per-pixel Python loop and a 1280x720 frame takes tens of seconds.
# On this machine /usr/bin/python3 has both while the PATH python3 does not.
PY=python3
for cand in python3 /usr/bin/python3 /opt/homebrew/bin/python3; do
  command -v "$cand" >/dev/null 2>&1 || continue
  if "$cand" -c "import numpy, PIL" >/dev/null 2>&1; then PY="$cand"; break; fi
done

MODE="${1:-}"; ARG="${2:-}"; STRENGTH="${3:-0.6}"
[ -n "$MODE" ] && [ -n "$ARG" ] || { sed -n '2,22p' "$0"; exit 2; }
[ -x ./build/wuxing ] || { echo "build/wuxing not found — build it first (cmake --build build -j4)"; exit 1; }

CFG=tuning.cfg
BAK=tuning.cfg.tonemap_ab.bak
OUT=autotest_output/tonemap_ab
KEY=postfx_hue_restore

# Never clobber a backup: if one is here, a previous run died and tuning.cfg is
# already the script's, not the user's.
[ -e "$BAK" ] && { echo "$BAK exists — a previous run did not finish. Restore it by hand first."; exit 1; }
[ -e "$CFG" ] && cp "$CFG" "$BAK"
restore() { [ -e "$BAK" ] && mv -f "$BAK" "$CFG"; }
trap restore EXIT INT TERM

set_knob() {
  [ -e "$BAK" ] && grep -v "^[[:space:]]*$KEY[[:space:]]*=" "$BAK" > "$CFG" || : > "$CFG"
  echo "$KEY = $1" >> "$CFG"
}

# Headless runs on macOS need the MoltenVK ICD on the loader's path or the game dies at
# instance creation. Deliberately NOT forced up front: if the game already runs here, a
# forced ICD can break a working setup. Instead the first capture retries once with it.
VSDK="${VULKAN_SDK:-$(ls -d "$HOME"/VulkanSDK/*/macOS 2>/dev/null | sort | tail -1 || true)}"
VKENV=()
[ -n "${VSDK:-}" ] && [ -d "$VSDK" ] && VKENV=(
  VK_ICD_FILENAMES="$VSDK/share/vulkan/icd.d/MoltenVK_icd.json" DYLD_LIBRARY_PATH="$VSDK/lib")
USE_VKENV=0

run_game() {
  if [ "$USE_VKENV" = "1" ] && [ ${#VKENV[@]} -gt 0 ]; then env "${VKENV[@]}" "$@" >/dev/null 2>&1 || true
  else "$@" >/dev/null 2>&1 || true; fi
}

capture() {   # capture <destination-png> ; assumes the knob is already set
  rm -rf "$OUT/staging"; mkdir -p "$OUT/staging"
  if [ "$MODE" = "--vfx" ]; then
    run_game ./build/wuxing --render-vfx "$ARG" --out "$OUT/staging/shot.png"
  else
    # Clear first, then pick by NAME. Picking the newest file by mtime out of a
    # directory that still holds the previous run's shots is how an A/B silently
    # compares a capture against itself when one of the two runs fails.
    rm -f autotest_output/verify_*.png
    WUXING_VERIFY="$ARG" run_game ./build/wuxing
    LAST=$(ls -1 autotest_output/verify_*.png 2>/dev/null | tail -1 || true)
    [ -n "$LAST" ] && cp "$LAST" "$OUT/staging/shot.png"
  fi
  if [ ! -f "$OUT/staging/shot.png" ] && [ "$USE_VKENV" = "0" ] && [ ${#VKENV[@]} -gt 0 ]; then
    echo "  (no capture — retrying with the MoltenVK ICD from $VSDK)"
    USE_VKENV=1
    capture "$1"
    return
  fi
  [ -f "$OUT/staging/shot.png" ] || {
    echo "no capture produced — is '$ARG' a valid ${MODE#--} argument? (skills: WATER TUBE METAL FIRE WOOD ELECTRIC WIND SHIELD)"
    echo "run it by hand to see the error:  WUXING_VERIFY=$ARG ./build/wuxing"
    exit 1; }
  mv "$OUT/staging/shot.png" "$1"
}

mkdir -p "$OUT"
echo "== A/A noise floor (both runs on the shipping curve) =="
set_knob 0
capture "$OUT/a0.png"
capture "$OUT/a1.png"
"$PY" scripts/diff_captures.py "$OUT/a0.png" "$OUT/a1.png" --label "A/A  noise floor"

echo "== A/B (shipping curve vs candidate at $KEY = $STRENGTH) =="
set_knob "$STRENGTH"
capture "$OUT/b.png"
"$PY" scripts/diff_captures.py "$OUT/a0.png" "$OUT/b.png" \
        --label "A/B  hue_restore=$STRENGTH" --out "$OUT/diff.png"

echo
echo "Captures in $OUT/ (a0=shipping, b=candidate, diff=x8 difference map)."
echo "Read it as: A/B must be far above the A/A floor to mean anything, and the changed"
echo "pixels must land on emissive content. tuning.cfg has been restored."
