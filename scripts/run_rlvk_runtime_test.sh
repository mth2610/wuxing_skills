#!/bin/bash
# Builds and runs the rlvk headless runtime test (third_party/vulkan/tests/rlvk_runtime_test.c)
# against MoltenVK with validation layers. Exercises: device init (1.1 fallbacks), texture
# staging roundtrips, SSBO roundtrips, compute dispatch, shaderc compile, clean shutdown.
# Requires the LunarG Vulkan SDK (installed at ~/VulkanSDK/<version> — see RLVK_HANDOFF.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VSDK="${VULKAN_SDK:-$(ls -d "$HOME"/VulkanSDK/*/macOS 2>/dev/null | sort | tail -1)}"
[ -d "$VSDK" ] || { echo "Vulkan SDK not found (set VULKAN_SDK)"; exit 1; }

CACHE="${RLVK_CHECK_CACHE:-/tmp/rlvk_check_cache}"
# raylib headers come from the compile-check cache (fetched on first run)
[ -f "$CACHE/raylib.h" ] || "$ROOT/scripts/check_rlvk_compile.sh" >/dev/null

BIN="$CACHE/rlvk_runtime_test"
cc "$ROOT/third_party/vulkan/tests/rlvk_runtime_test.c" -o "$BIN" \
    -I"$CACHE" -I"$VSDK/include" \
    -I"$ROOT/third_party/vulkan" -I"$ROOT/third_party/vulkan/include" \
    -L"$VSDK/lib" -lvulkan -Wl,-rpath,"$VSDK/lib" -std=c11

cd "$CACHE"   # pipeline-cache file lands here, not in the repo
VK_ICD_FILENAMES="$VSDK/share/vulkan/icd.d/MoltenVK_icd.json" \
VK_LAYER_PATH="$VSDK/share/vulkan/explicit_layer.d" \
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
DYLD_LIBRARY_PATH="$VSDK/lib" \
"$BIN" "$@"
