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
 * Nothing else left on the table (culling, clears, exposure chain) is worth a
 * millisecond without deleting visible content — see ENGINE_LANDMINES.md
 * ("A resolution A/B prices a clear plus its fill").
 *
 * The scene chain renders at `scale * display` and PostFX's composite upscales
 * it to the swapchain (that blit already existed for the Android native-size
 * mismatch). FXAA applies a luma unsharp mask scaled by how far below 1.0 the
 * rung is, so the upscale does not read as blur; it is exactly 0.0 at 1.00.
 *
 * HOW THE RUNG IS CHOSEN — one decision per map, then frozen
 * ----------------------------------------------------------
 * Every rung change reallocates the scene chain and the post chain (~35 MB at
 * 720p) and rebuilds six framebuffers. That is a hitch of tens of milliseconds
 * and a window for the compositor to present a blank frame — and because the
 * frame sits right next to the 16.67 ms refresh, moving the rung by 1.5 ms can
 * flip the delivered rate between one and two refreshes. A controller that
 * tracks the budget therefore oscillates 60 -> 30 fps and flashes the screen on
 * every step. Both of those were observed in play with the first version.
 *
 * So: start at 0.85 (the measured sweet spot on this GPU, and the rung most
 * likely to be kept, so the common case never resizes at all), measure the
 * submit time for ~1 s, pick the rung that predicts ~13.8 ms submit
 * (~15.0 ms delivered: under the refresh with ~1.7 ms of slack), apply that
 * ONE step, and freeze. A frozen rung may still be stepped DOWN if the frame
 * stays above 16.0 ms submit for ~2 s (a fight, thermal throttling), at most
 * twice per map. It never steps up.
 *
 * The next map gets a fresh calibration (RenderScale_NotifyMapChange).
 *
 * IMPORTANT — what is measured. RenderScale_Frame is fed the SUBMIT time (top
 * of the frame -> just before EndDrawing), which excludes the vsync wait. With
 * `SetTargetFPS(60)` a delivered interval saturates at the refresh and cannot
 * tell a 9 ms frame from a 16.5 ms one, so the interval is not used at all.
 * ------------------------------------------------------------------------- */

#include <stdbool.h>

// One-time setup. `displayWidth/Height` are the swapchain size (GetRenderWidth).
void  RenderScale_Init(int displayWidth, int displayHeight);

/* Call ONCE per frame, after drawing, with:
 *   submitMs  — wall time of the frame up to just before present (no vsync wait)
 *   intervalMs— the delivered interval; accepted but not used (see above)
 *   displayW/H— current swapchain size (0 to keep the last one)
 * Updates the decision. The new size is picked up by RenderScale_TakeResize. */
void  RenderScale_Frame(float submitMs, float intervalMs, int displayW, int displayH);

/* Call when the active map changes: restarts the 1 s calibration so a cheaper or
 * heavier map is measured rather than inherited. Keeps the current rung as the
 * starting point, so the common case costs no resize at all. */
void  RenderScale_NotifyMapChange(void);

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
 * reproducible), so the calibration and the resize path can be exercised
 * without a window. */
bool  RenderScale_AdaptiveForced(void);

#endif
