#!/bin/bash
# Builds and runs the rlvk windowed visual test suite (third_party/vulkan/tests/rlvk_visual_test.c)
# against MoltenVK. One PASS/FAIL line per scenario; exit code = number of failures.
#
#   ./scripts/run_rlvk_visual_test.sh                 # all scenarios
#   ./scripts/run_rlvk_visual_test.sh depth_rt        # one scenario
#   ./scripts/run_rlvk_visual_test.sh --list          # scenario names
#   VALIDATE=1 ./scripts/run_rlvk_visual_test.sh ...  # with Khronos validation layers
#   UNCAPPED=1 ./scripts/run_rlvk_visual_test.sh perf_rt2048   # IMMEDIATE present (no vsync)
#              for the perf_* probes: FIFO quantizes every frame time to the refresh interval,
#              which makes any measurement useless. Uses its own raylib cache dir.
#
# First run clones + builds a Vulkan-patched raylib 6.0 into /tmp/rlvk_visual_cache
# (out of the repo, ~2 min); later runs only recompile rlvk + the test (~15 s).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VSDK="${VULKAN_SDK:-$(ls -d "$HOME"/VulkanSDK/*/macOS 2>/dev/null | sort | tail -1)}"
[ -d "$VSDK" ] || { echo "Vulkan SDK not found (set VULKAN_SDK)"; exit 1; }

# PERFORMANCE_CAPTURE switches the swapchain to IMMEDIATE present (rlvk_platform.inl) so the
# perf_* probes measure real frame cost instead of the vsync interval. Separate cache: it is a
# different raylib build and must not clobber the normal one.
EXTRA_CFLAGS=""
CACHE_SUFFIX=""
if [ "${UNCAPPED:-0}" = "1" ]; then
    EXTRA_CFLAGS="-DPERFORMANCE_CAPTURE"
    CACHE_SUFFIX="_uncapped"
fi

CACHE="${RLVK_VISUAL_CACHE:-/tmp/rlvk_visual_cache$CACHE_SUFFIX}"
RAYLIB="$CACHE/raylib"
BUILD="$CACHE/raylib-build"
mkdir -p "$CACHE"

if [ ! -d "$RAYLIB" ]; then
    echo "fetching raylib 6.0 into $RAYLIB ..."
    git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git "$RAYLIB" >/dev/null
fi
python3 "$ROOT/scripts/rlvk_patch_raylib.py" "$RAYLIB" >/dev/null

# rcore.c textually includes rlvk.h -> rebuild whenever any rlvk source is newer
NEWEST_RLVK=$(ls -t "$ROOT/third_party/vulkan/rlvk.h" "$ROOT/third_party/vulkan/rlvk"/*.inl | head -1)
if [ ! -f "$BUILD/raylib/libraylib.a" ] || [ "$NEWEST_RLVK" -nt "$BUILD/raylib/libraylib.a" ]; then
    cmake -S "$RAYLIB" -B "$BUILD" -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_FLAGS="$EXTRA_CFLAGS -DGRAPHICS_API_VULKAN -I$ROOT/third_party/vulkan -I$ROOT/third_party/vulkan/include -I$VSDK/include" \
        >/dev/null
    touch "$RAYLIB/src/rcore.c"
    cmake --build "$BUILD" -j4 >/dev/null
fi

BIN="$CACHE/rlvk_visual_test"
cc "$ROOT/third_party/vulkan/tests/rlvk_visual_test.c" -o "$BIN" \
    -I"$RAYLIB/src" -L"$BUILD/raylib" -lraylib \
    -L"$VSDK/lib" -lvulkan -Wl,-rpath,"$VSDK/lib" \
    -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreFoundation -framework QuartzCore -lm

cd "$CACHE"   # pipeline-cache/screenshot files land here, not in the repo
ENV=(VK_ICD_FILENAMES="$VSDK/share/vulkan/icd.d/MoltenVK_icd.json" DYLD_LIBRARY_PATH="$VSDK/lib")
if [ "${VALIDATE:-0}" = "1" ]; then
    ENV+=(VK_LAYER_PATH="$VSDK/share/vulkan/explicit_layer.d" VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation)
fi
env "${ENV[@]}" "$BIN" "$@"
