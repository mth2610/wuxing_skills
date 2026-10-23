#ifndef RENDER_SCALE_H
#define RENDER_SCALE_H

/* ADAPTIVE RENDER SCALE
 * ---------------------------------------------------------------------------
 * The frame is fill-bound on the target hardware: shading cost is proportional
 * to the number of pixels, so the only lever left that moves whole milliseconds
 * is the scene chain's resolution. Measured on the song_quao frame (1280x720,
 * HIGH, Vulkan/MoltenVK, 3 paired runs): 1.00 = 16.83 ms, 0.85 = 13.78 ms
 * (-18 %). Rendering the WHOLE window smaller is worth more still (0.80 =
 * -5.0 ms) but that also shrinks the UI and the present blit, which this does
 * not — only the scene and post chains scale.
 * Nothing else that was still on the table (culling, clears, exposure chain)
 * is worth a millisecond without deleting visible content — see
 * ENGINE_LANDMINES.md ("A resolution A/B prices a clear plus its fill").
 *
 * The scene chain renders at `scale * display` and PostFX's composite upscales
 * it to the swapchain (that blit already existed for the Android native-size
 * mismatch), so the only visible cost is 5 % of sharpness at 0.95 — invisible
 * at 1:1, a coarser texel grid when zoomed.
 *
 * The controller earns its keep because the cost is scene-dependent: the night
 * arena runs the budget flat out, a fight or a bright map does not. It steps
 * the ladder down when the measured frame time is over budget and back up when
 * it is comfortably under, with hysteresis so a step never flaps.
 *
 * IMPORTANT — what is measured. `RenderScale_Frame` is fed the SUBMIT time
 * (frame start -> just before EndDrawing), which excludes the vsync wait. With
 * `SetTargetFPS(60)` a delivered interval saturates at 16.7 ms and cannot tell
 * a 9 ms frame from a 16.5 ms one, so the interval is used only as a
 * confirmation that a step did not break the target (it must stay at one
 * refresh); the decision itself uses the submit time.
 * ------------------------------------------------------------------------- */

#include <stdbool.h>

// One-time setup. `displayWidth/Height` are the swapchain size (GetRenderWidth).
void  RenderScale_Init(int displayWidth, int displayHeight);

/* Call ONCE per frame, after drawing, with:
 *   submitMs  — wall time of the frame up to just before present (no vsync wait)
 *   intervalMs— the delivered interval (GetFrameTime()) for reference only
 *   displayW/H— current swapchain size (0 to keep the last one)
 * Updates the decision. The new size is picked up by RenderScale_TakeResize. */
void  RenderScale_Frame(float submitMs, float intervalMs, int displayW, int displayH);

/* Call at the TOP of the frame, before SceneTargets_Begin. Returns true when the
 * target size changed since the last call and writes the size to use. The caller
 * must then resize the scene chain (SceneTargets_Resize / PostFX_Resize) before
 * drawing anything. */
bool  RenderScale_TakeResize(int *width, int *height);

int   RenderScale_GetWidth(void);
int   RenderScale_GetHeight(void);
float RenderScale_GetScale(void);
bool  RenderScale_IsAdaptive(void);

/* True when WUXING_RENDER_SCALE_ADAPTIVE is set: forces the controller on in
 * the headless capture paths (which otherwise pin a fixed scale so a capture is
 * reproducible), so the ladder and the resize path can be exercised without a
 * window. */
bool  RenderScale_AdaptiveForced(void);

#endif
