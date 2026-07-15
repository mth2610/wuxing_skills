// rlvk_bisect.c — windowed visual bisect test for the Vulkan backend.
// Draws: red clear + green triangle (RL_TRIANGLES) + blue rectangle (RL_QUADS).
// After N frames (argv[1], default 2) takes a screenshot "bisect.png" and exits.
// Expected result: red background, green triangle in the upper half, blue rect lower half.
//
// Build (macOS MoltenVK):
//   SCRATCH=<scratchpad_dir>   VSDK=$HOME/VulkanSDK/<ver>/macOS
//   touch $SCRATCH/raylib/src/rcore.c && cmake --build $SCRATCH/raylib-build -j4
//   cc rlvk_bisect.c -o rlvk_bisect -I$SCRATCH/raylib/src -L$SCRATCH/raylib-build/raylib \
//      -lraylib -L$VSDK/lib -lvulkan -Wl,-rpath,$VSDK/lib \
//      -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreFoundation \
//      -framework QuartzCore
//
// Run:
//   VK_ICD_FILENAMES=$VSDK/share/vulkan/icd.d/MoltenVK_icd.json \
//   DYLD_LIBRARY_PATH=$VSDK/lib ./rlvk_bisect 2
//   open bisect.png

#include "raylib.h"
#include "rlgl.h"
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int warmupFrames = (argc > 1) ? atoi(argv[1]) : 2;
    if (warmupFrames < 1) warmupFrames = 1;

    const int W = 640, H = 480;
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(W, H, "rlvk bisect");

    for (int frame = 0; frame < warmupFrames + 1; frame++)
    {
        BeginDrawing();
        ClearBackground((Color){ 200, 40, 40, 255 });   // red-ish

        // --- Green triangle via RL_TRIANGLES (batch path) ---
        rlBegin(RL_TRIANGLES);
            rlColor4ub(0, 220, 60, 255);
            rlVertex3f(W*0.5f,  H*0.1f, 0);   // top-center
            rlVertex3f(W*0.2f,  H*0.45f, 0);   // bottom-left
            rlVertex3f(W*0.8f,  H*0.45f, 0);   // bottom-right
        rlEnd();

        // --- Blue rectangle via RL_QUADS (batch path, indexed) ---
        rlBegin(RL_QUADS);
            rlColor4ub(40, 60, 220, 255);
            rlVertex3f(W*0.25f, H*0.55f, 0);   // top-left
            rlVertex3f(W*0.25f, H*0.85f, 0);   // bottom-left
            rlVertex3f(W*0.75f, H*0.85f, 0);   // bottom-right
            rlVertex3f(W*0.75f, H*0.55f, 0);   // top-right
        rlEnd();

        EndDrawing();

        if (frame == warmupFrames)
        {
            TakeScreenshot("bisect.png");
            printf("bisect: screenshot taken after %d frame(s)\n", warmupFrames + 1);
        }
    }

    CloseWindow();
    return 0;
}
