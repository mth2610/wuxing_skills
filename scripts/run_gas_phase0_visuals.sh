#!/usr/bin/env bash
# Deterministic Phase 0 visual sweep for the single GAS PLUME fixture.
# Produces smoke/fire/energy matrices with bloom forced off and on. Override the
# whitespace-separated sets to run a smaller diagnostic slice.
set -euo pipefail
cd "$(dirname "$0")/.."

WARMUP="${WUXING_GAS_WARMUP:-90}"
QUALITY="${WUXING_GFX_QUALITY:-med}"
KINDS="${WUXING_GAS_KINDS:-smoke fire energy}"
BLOOM_MODES="${WUXING_GAS_BLOOM_MODES:-off on}"
OUT_ROOT="${WUXING_GAS_PHASE0_OUT:-autotest_output/gas_phase0}"
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/wuxing-gas-phase0.XXXXXX")
trap 'rm -rf "$TMP_ROOT"' EXIT

for kind in $KINDS; do
  case "$kind" in
    smoke) kind_value=0 ;;
    fire) kind_value=1 ;;
    energy) kind_value=2 ;;
    *) echo "unknown gas kind: $kind"; exit 2 ;;
  esac
  for bloom in $BLOOM_MODES; do
    case "$bloom" in
      off) bloom_value=0 ;;
      on) bloom_value=1 ;;
      *) echo "unknown bloom mode: $bloom"; exit 2 ;;
    esac
    config="$TMP_ROOT/${kind}_${bloom}.cfg"
    printf 'gasplume_kind_override = %s\npostfx_bloom = %s\ngas_perf_log = 1\n' \
      "$kind_value" "$bloom_value" > "$config"
    echo "GAS PHASE 0: kind=$kind bloom=$bloom quality=$QUALITY warmup=$WARMUP"
    env WUXING_TUNING="$config" WUXING_GFX_QUALITY="$QUALITY" \
        WUXING_VFX_MATRIX_OUT="$OUT_ROOT/${kind}_${bloom}_${QUALITY}" \
        scripts/render_vfx_matrix.sh "GAS PLUME" "$WARMUP"
  done
done
