#!/bin/bash
# Compile-checks third_party/vulkan/rlvk.h standalone (no Vulkan SDK / no link needed).
# Fetches header deps (Vulkan-Headers, raylib.h 6.0) into a cache dir on first run.
# Usage: ./scripts/check_rlvk_compile.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="${RLVK_CHECK_CACHE:-/tmp/rlvk_check_cache}"
mkdir -p "$CACHE"

if [ ! -f "$CACHE/vk-headers/include/vulkan/vulkan.h" ]; then
    git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git "$CACHE/vk-headers"
fi
for f in raylib.h config.h rlgl.h; do
    if [ ! -f "$CACHE/$f" ]; then
        curl -sfL -o "$CACHE/$f" "https://raw.githubusercontent.com/raysan5/raylib/6.0/src/$f"
    fi
done

cat > "$CACHE/check_rlvk.c" <<'EOF'
// Mimics raylib's rcore.c compile context: raylib.h first, then the backend impl.
#include "raylib.h"
#define RLVK_IMPLEMENTATION
#include "rlvk.h"
int main(void) { return 0; }
EOF

cc -c "$CACHE/check_rlvk.c" -o /dev/null \
    -I"$CACHE" -I"$CACHE/vk-headers/include" \
    -I"$ROOT/third_party/vulkan" \
    -I"$ROOT/third_party/vulkan/include" \
    -std=c11 -Wall -Wno-unused-function

echo "rlvk.h compile check: OK"
