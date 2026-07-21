#include "core/trail_system.h"
#include "core/force_field.h"
#include "raylib.h"
#include "raymath.h"

void VFX_ComposeTrailRibbonTest(Vector3 pos)
{
    // ── 3 WISP trails with forceField, side-by-side ──
    // WISP initializes all history nodes immediately (always visible).
    // A simple gravity-direction forceField makes them drift so the
    // ribbon mode difference is apparent as they move.

    static ForceField s_driftFld[3];
    static bool s_init = false;
    if (!s_init) {
        for (int i = 0; i < 3; i++) {
            ForceField_Clear(&s_driftFld[i]);
            ForceField_AddLayer(&s_driftFld[i], (ForceLayer){
                .type = FORCE_GRAVITY_DIR,
                .direction = (Vector3){0.0f, -0.3f, -1.0f},
                .strength = 2.0f});
        }
        s_init = true;
    }

    // 1) Red — RIBBON_CAMERA_FACING (old default, pinches when camera aligns)
    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_WISP;
    cfg.pos = Vector3Add(pos, (Vector3){-1.2f, 1.2f, 0.0f});
    cfg.target = Vector3Add(cfg.pos, (Vector3){0.0f, 0.5f, -2.0f});
    cfg.vel = (Vector3){0.0f, 0.5f, -2.0f};
    cfg.len = 2.0f;
    cfg.thick = 0.14f;
    cfg.trailLength = 0.0f;
    cfg.life = 1.8f;
    cfg.tint = (Color){255, 80, 80, 220};
    cfg.forceField = &s_driftFld[0];
    cfg.ribbonMode = RIBBON_CAMERA_FACING;
    SpawnTrailEntity(cfg);

    // 2) Green — RIBBON_WORLD_UP (always vertical, no pinch)
    cfg.pos = Vector3Add(pos, (Vector3){0.0f, 1.2f, 0.0f});
    cfg.target = Vector3Add(cfg.pos, (Vector3){0.0f, 0.5f, -2.0f});
    cfg.vel = (Vector3){0.0f, 0.5f, -2.0f};
    cfg.tint = (Color){80, 255, 80, 220};
    cfg.forceField = &s_driftFld[1];
    cfg.ribbonMode = RIBBON_WORLD_UP;
    SpawnTrailEntity(cfg);

    // 3) Blue — RIBBON_FIXED_NORMAL (tilted custom normal)
    cfg.pos = Vector3Add(pos, (Vector3){1.2f, 1.2f, 0.0f});
    cfg.target = Vector3Add(cfg.pos, (Vector3){0.0f, 0.5f, -2.0f});
    cfg.vel = (Vector3){0.0f, 0.5f, -2.0f};
    cfg.tint = (Color){80, 80, 255, 220};
    cfg.forceField = &s_driftFld[2];
    cfg.ribbonMode = RIBBON_FIXED_NORMAL;
    cfg.fixedNormal = (Vector3){0.0f, 1.0f, 0.3f};
    SpawnTrailEntity(cfg);
}
