#include "skills/fire/liet_diem_tran_skill/liet_diem_tran_skill.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "core/vfx_light.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

// Liet Diem Tran (Blazing Inferno Formation) — Fire, medium-range AoE/control
// "Tram trung" formation: a ring of flame pillars ignites around the target
// point, pulsing knockback + damage while active. See nguhanhtyvo_kehoach.md
// line 78 ("Liet diem tran" listed as the Fire example for this category).

#define MAX_INSTANCES 4
#define PILLAR_COUNT 8
#define FLAME_SEGMENTS 4

#define CASTING_DURATION 0.35f
#define RISING_DURATION 0.5f
#define ACTIVE_DURATION 3.0f
#define DISSOLVE_DURATION 0.6f
#define TICK_INTERVAL 0.6f

#define IGNITE_DAMAGE 22.0f
#define IGNITE_KNOCKBACK 600.0f
#define TICK_DAMAGE 14.0f
#define TICK_KNOCKBACK 480.0f

typedef enum {
    STATE_CASTING,
    STATE_RISING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;   // formation center, fixed at the cast target
    float stateTimer;
    float tickTimer;
    float ringRadius;
    float pillarMaxHeight;
    float currentHeight; // shared rise/dissolve height for pillars + core rune
    float sizeScale;
    float spinAngle;
    float pillarYaw[PILLAR_COUNT];   // per-pillar perpendicular jitter (12.2)
    float pillarScale[PILLAR_COUNT]; // per-pillar 85-115% scale (12.3)
    float pillarLean[PILLAR_COUNT];  // per-pillar +-10 deg lean (12.3)
    int emitterId;
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void InitLietDiemTranSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastLietDiemTranSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;

        float sizeScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

        SkillInstance *s = &s_instances[i];
        s->state = STATE_CASTING;
        s->position = target; // formation rises on the drawn target, medium range from caster
        s->stateTimer = 0.0f;
        s->tickTimer = 0.0f;
        s->ringRadius = 150.0f * sizeScale;
        s->pillarMaxHeight = 90.0f * sizeScale;
        s->currentHeight = 0.0f;
        s->sizeScale = sizeScale;
        s->spinAngle = 0.0f;
        s->emitterId = -1;

        for (int p = 0; p < PILLAR_COUNT; p++) {
            s->pillarYaw[p] = (float)GetRandomValue(-120, 120) / 10.0f; // perpendicular jitter, §12.2
            s->pillarScale[p] = (float)GetRandomValue(85, 115) / 100.0f;
            s->pillarLean[p] = (float)GetRandomValue(-10, 10);
        }

        s->active = true;
        SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, sizeScale * 0.6f);
        return;
    }
}

void UpdateLietDiemTranSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;
        s->spinAngle += dt * 70.0f;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= CASTING_DURATION) {
                    s->state = STATE_RISING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_RISING: {
                float t = SmoothStep01(s->stateTimer / RISING_DURATION);
                s->currentHeight = Math_Mix(0.0f, s->pillarMaxHeight, t);
                if (s->stateTimer >= RISING_DURATION) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    s->tickTimer = 0.0f;
                    s->currentHeight = s->pillarMaxHeight;

                    SpawnImpactEffect(s->position, EFFECT_PRESET_FIRE_EXPLOSION, s->sizeScale);
                    SpawnGroundDecal(DECAL_PRESET_FIRE_LAVA, s->position, s->ringRadius * 0.9f,
                                      ACTIVE_DURATION + DISSOLVE_DURATION);
                    VFXLight_Spawn(s->position, ELEMENT_COLOR_FIRE, 120.0f * s->sizeScale, ACTIVE_DURATION);
                    s->emitterId = Emitter_AttachToPoint(EMITTER_FIRE, s->position, 45.0f, ACTIVE_DURATION);

                    // Ignition detonation — the strongest push-back, right as the formation lights up.
                    Entity_ApplyAoEDamage(s->position, s->ringRadius, IGNITE_DAMAGE, IGNITE_KNOCKBACK);
                }
                break;
            }

            case STATE_ACTIVE: {
                s->tickTimer += dt;
                if (s->tickTimer >= TICK_INTERVAL) {
                    s->tickTimer -= TICK_INTERVAL;
                    Entity_ApplyAoEDamage(s->position, s->ringRadius, TICK_DAMAGE, TICK_KNOCKBACK);
                }
                if (s->stateTimer >= ACTIVE_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                    if (s->emitterId >= 0) {
                        Emitter_Stop(s->emitterId);
                        s->emitterId = -1;
                    }
                }
                break;
            }

            case STATE_DISSOLVE: {
                float t = SmoothStep01(s->stateTimer / DISSOLVE_DURATION);
                s->currentHeight = Math_Mix(s->pillarMaxHeight, 0.0f, t);
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
            }
        }
    }
}

void DrawLietDiemTranSkill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->currentHeight <= 0.01f) continue;

        Color darkRoot = ColorLerp(ELEMENT_COLOR_FIRE, BLACK, 0.45f);
        Color hotTip = ColorLerp(ELEMENT_COLOR_FIRE, WHITE, 0.55f);

        // 8 flame pillars ringing the formation center, each built from several
        // short, per-frame-jittered segments (§12.2) shaded dark-root -> hot-tip
        // (§ Rules for Color Usage) so they read as licking flames, not solid cones.
        for (int p = 0; p < PILLAR_COUNT; p++) {
            float angle = ((float)p / (float)PILLAR_COUNT) * 2.0f * PI;
            Vector3 dir = { cosf(angle), 0.0f, sinf(angle) };
            Vector3 perp = { -dir.z, 0.0f, dir.x };

            Vector3 anchor = Vector3Add(s->position, Vector3Scale(dir, s->ringRadius));
            anchor = Vector3Add(anchor, Vector3Scale(perp, s->pillarYaw[p]));

            float pillarHeight = s->currentHeight * s->pillarScale[p];
            float leanRad = s->pillarLean[p] * DEG2RAD;
            float baseRadius = 12.0f * s->sizeScale * s->pillarScale[p];

            Vector3 segBottom = anchor;
            for (int seg = 0; seg < FLAME_SEGMENTS; seg++) {
                float segT = (float)seg / (float)(FLAME_SEGMENTS - 1);
                float segHeight = (pillarHeight / FLAME_SEGMENTS) * (1.0f - segT * 0.15f);

                // Recomputed every frame -> reads as flicker, not a rigid static shape.
                float flicker = (float)GetRandomValue(-25, 25) / 10.0f;
                Vector3 segTop = Vector3Add(segBottom, Vector3Scale(perp, pillarHeight * tanf(leanRad) / FLAME_SEGMENTS + flicker));
                segTop.y = segBottom.y + segHeight;

                float radiusBottom = Math_Mix(baseRadius, baseRadius * 0.15f, segT);
                float radiusTop = Math_Mix(baseRadius, baseRadius * 0.15f, segT + 1.0f / FLAME_SEGMENTS);
                Color segColor = ColorLerp(darkRoot, hotTip, segT);

                DrawCoreCylinder(segBottom, segTop, radiusBottom, radiusTop, 8, segColor);
                segBottom = segTop;
            }

            DrawCoreSphere(segBottom, baseRadius * 0.35f, 6, 6, hotTip);
        }

        // Rotating ground rune ring at the base of the formation (the "Tran" glyph).
        Color runeColor = ColorAlpha(ColorLerp(ELEMENT_COLOR_FIRE, WHITE, 0.2f), 0.55f);
        DrawCoreTorus(s->position, s->ringRadius * 0.94f, s->ringRadius, 10, 20, runeColor);

        // Central rising flame core — same segmented/gradient treatment as the ring pillars.
        Vector3 coreBottom = s->position;
        float coreHeight = s->currentHeight * 0.7f;
        for (int seg = 0; seg < FLAME_SEGMENTS; seg++) {
            float segT = (float)seg / (float)(FLAME_SEGMENTS - 1);
            float flickerX = (float)GetRandomValue(-30, 30) / 10.0f;
            float flickerZ = (float)GetRandomValue(-30, 30) / 10.0f;
            Vector3 coreTop = { coreBottom.x + flickerX, s->position.y + coreHeight * (segT + 1.0f / FLAME_SEGMENTS), coreBottom.z + flickerZ };
            float radiusBottom = Math_Mix(18.0f * s->sizeScale, 3.0f * s->sizeScale, segT);
            float radiusTop = Math_Mix(18.0f * s->sizeScale, 3.0f * s->sizeScale, segT + 1.0f / FLAME_SEGMENTS);
            Color segColor = ColorLerp(darkRoot, hotTip, segT);
            DrawCoreCylinder(coreBottom, coreTop, radiusBottom, radiusTop, 10, segColor);
            coreBottom = coreTop;
        }
    }
}

void UnloadLietDiemTranSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

bool IsLietDiemTranSkillCoiling(void) {
    return false; // no charge-up/coiling phase — the formation ignites in place
}

int GetLietDiemTranSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    (void)outProjectiles;
    (void)maxProjectiles;
    return 0; // ground-anchored formation, nothing flies
}

void DeactivateLietDiemTranProjectile(int index) {
    (void)index; // no-op: no flying projectiles to deactivate
}
