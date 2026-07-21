#include "core/trail_system.h"
#include "raylib.h"
#include "raymath.h"

void VFX_ComposeTrailRibbonTest(Vector3 pos)
{
    float time = (float)GetTime();

    // ── 3 WISP trails, each with a different RibbonMode ──
    // Use TRAIL_TYPE_WISP so nodes are pre-populated immediately at spawn.

    // 1) Red — RIBBON_CAMERA_FACING (old default, pinches when camera aligns)
    TrailConfig r = {0};
    r.type = TRAIL_TYPE_WISP;
    r.pos = Vector3Add(pos, (Vector3){-3.0f, 1.5f, 0.0f});
    r.target = Vector3Add(r.pos, (Vector3){sinf(time) * 2.0f, 0.5f, cosf(time) * 2.0f});
    r.vel = (Vector3){0, 0, 0};
    r.len = 3.0f;
    r.thick = 0.8f;
    r.trailLength = 30;
    r.life = 4.0f;
    r.tint = (Color){255, 60, 60, 220};
    r.ribbonMode = RIBBON_CAMERA_FACING;
    SpawnTrailEntity(r);

    // 2) Green — CAMERA_FACING (same mode as red, to verify green config works)
    TrailConfig g = {0};
    g.type = TRAIL_TYPE_WISP;
    g.pos = Vector3Add(pos, (Vector3){0.0f, 1.5f, 0.0f});
    g.target = Vector3Add(g.pos, (Vector3){sinf(time + 2.0f) * 2.0f, 0.5f, cosf(time + 2.0f) * 2.0f});
    g.vel = (Vector3){0, 0, 0};
    g.len = 3.0f;
    g.thick = 0.8f;
    g.trailLength = 30;
    g.life = 4.0f;
    g.tint = (Color){60, 255, 60, 220};
    g.ribbonMode = RIBBON_CAMERA_FACING;
    SpawnTrailEntity(g);

    // 3) Blue — CAMERA_FACING (same mode as red, to verify blue config works)
    TrailConfig b = {0};
    b.type = TRAIL_TYPE_WISP;
    b.pos = Vector3Add(pos, (Vector3){3.0f, 1.5f, 0.0f});
    b.target = Vector3Add(b.pos, (Vector3){sinf(time + 4.0f) * 2.0f, 0.5f, cosf(time + 4.0f) * 2.0f});
    b.vel = (Vector3){0, 0, 0};
    b.len = 3.0f;
    b.thick = 0.8f;
    b.trailLength = 30;
    b.life = 4.0f;
    b.tint = (Color){60, 60, 255, 220};
    b.ribbonMode = RIBBON_CAMERA_FACING;
    SpawnTrailEntity(b);
}
