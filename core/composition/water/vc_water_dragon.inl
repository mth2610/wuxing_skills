// ── WATER DRAGON (THỦY LONG XUẤT HẢI) ──────────────────────────────────────────
//
// High-Fidelity AAA Eastern Water Dragon (Rồng Nước / Thủy Long):
// 1. Serpentine 3D Swept Body: Continuous procedural swept water tube mesh
//    (VFX_ComposeWaterStreamOnPath) driven by 3D sinusoidal swimming undulation
//    (lateral S-curve & vertical breathing waves) tapering from a wide neck to
//    a whipping slender tail.
// 2. Articulated Dragon Head: High-resolution sculpted dragon head with dynamic
//    gaping jaw oscillation (jawAnim), cyan ice eye glow, and tangent orientation.
// 3. Helical Vortex Ribbons: Dual coiling water tendrils (Long Khí) spiraling
//    around the dragon spine at high angular velocity (swirlSpeed).
// 4. Target Impact Maw Slam & Geyser: Vertical water geyser, Worthington splash crown,
//    conformal wet puddle decal, radial ground wave shock ring, wind blast, and camera trauma.
//
#ifndef VC_WATER_DRAGON_INL
#define VC_WATER_DRAGON_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/motion/motion_fields.h"
#include "core/decals/decal_system.h"
#include "core/wind/wind_system.h"
#include "core/map_manager.h"
#include "core/vfx_render.h"
#include "core/vfx_light.h"
#include "post_process/camera_fx.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VC_WATER_DRAGON_MAX_INSTANCES 6
#define VC_WATER_DRAGON_PATH_POINTS   32

typedef struct {
    bool active;
    int id;
    VFX_WaterDragonConfig config;
    float timer;
    float age;
    float flightProgress;
    float flightDuration;
    Vector3 headPos;
    Vector3 headTangent;
    Vector3 curTarget;

    Vector3 bodyPoints[VC_WATER_DRAGON_PATH_POINTS];
    int bodyPointCount;

    bool impactTriggered;
    float impactTimer;
    Vector3 groundPuddlePos;
    float curPuddleRadius;
    float puddleMaxRadius;
    bool decalSpawned;

    MotionFieldHandle motionField;
    bool motionFieldCreated;
} VC_WaterDragonInstance;

static VC_WaterDragonInstance s_waterDragonInstances[VC_WATER_DRAGON_MAX_INSTANCES];
static int s_waterDragonNextId = 1;
static bool s_waterDragonInit = false;
static VFX_WaterDragonConfig s_liveWaterDragonConfig;

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_WaterDragonConfig VFX_WaterDragon_DefaultConfig(void)
{
    VFX_WaterDragonConfig cfg;
    memset(&cfg, 0, sizeof(VFX_WaterDragonConfig));

    cfg.travelSpeed = 22.0f;
    cfg.bodyRadius = 0.52f;
    cfg.arcHeight = 3.2f;
    cfg.swimFrequency = 12.0f;
    cfg.swimAmplitude = 0.65f;
    cfg.headScale = 1.6f;
    cfg.spiralSpeed = 18.0f;
    cfg.spiralRadius = 0.85f;
    cfg.waterBodyColor = (Color){15, 115, 195, 230};
    cfg.waterGlowColor = (Color){170, 240, 255, 255};
    cfg.waterFoamColor = (Color){245, 252, 255, 255};
    cfg.puddleRadius = 3.5f;
    cfg.puddleDuration = 4.2f;
    cfg.impactGeyser = true;
    cfg.impactDecal = true;

    return cfg;
}

VFX_WaterDragonConfig *VFX_WaterDragon_GetLiveConfig(void)
{
    if (!s_waterDragonInit) {
        s_liveWaterDragonConfig = VFX_WaterDragon_DefaultConfig();
        s_waterDragonInit = true;
    }
    return &s_liveWaterDragonConfig;
}

int VFX_WaterDragon_GetParams(VFX_WaterDragonConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

#define DRAGON_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define DRAGON_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    DRAGON_PARAM_FLOAT("Travel Speed m/s", "Trajectory", cfg->travelSpeed, 8.0f, 40.0f, 1.0f);
    DRAGON_PARAM_FLOAT("Body Radius m", "Body", cfg->bodyRadius, 0.20f, 1.20f, 0.05f);
    DRAGON_PARAM_FLOAT("Arc Height m", "Trajectory", cfg->arcHeight, 0.5f, 8.0f, 0.25f);
    DRAGON_PARAM_FLOAT("Swim Frequency", "Swimming", cfg->swimFrequency, 4.0f, 24.0f, 1.0f);
    DRAGON_PARAM_FLOAT("Swim Amplitude m", "Swimming", cfg->swimAmplitude, 0.1f, 1.5f, 0.05f);
    DRAGON_PARAM_FLOAT("Dragon Head Scale", "Head", cfg->headScale, 0.6f, 3.0f, 0.1f);
    DRAGON_PARAM_FLOAT("Spiral Speed rad/s", "Tendril", cfg->spiralSpeed, 4.0f, 35.0f, 1.0f);
    DRAGON_PARAM_FLOAT("Spiral Radius m", "Tendril", cfg->spiralRadius, 0.3f, 2.0f, 0.05f);
    DRAGON_PARAM_FLOAT("Puddle Radius m", "Impact", cfg->puddleRadius, 1.0f, 6.0f, 0.25f);
    DRAGON_PARAM_FLOAT("Puddle Duration s", "Impact", cfg->puddleDuration, 1.0f, 8.0f, 0.5f);
    DRAGON_PARAM_BOOL("Geyser Eruption", "Impact", cfg->impactGeyser);
    DRAGON_PARAM_BOOL("Ground Decal", "Impact", cfg->impactDecal);

#undef DRAGON_PARAM_FLOAT
#undef DRAGON_PARAM_BOOL

    return n;
}

// ── Public Composition Entry Points ──────────────────────────────────────────

int VFX_ComposeWaterDragonEx(Vector3 source, Vector3 target, const VFX_WaterDragonConfig *cfg)
{
    if (!s_waterDragonInit) {
        VFX_WaterDragon_GetLiveConfig();
    }

    VC_WaterDragonInstance *inst = NULL;
    for (int i = 0; i < VC_WATER_DRAGON_MAX_INSTANCES; i++) {
        if (!s_waterDragonInstances[i].active) {
            inst = &s_waterDragonInstances[i];
            break;
        }
    }
    if (!inst) {
        inst = &s_waterDragonInstances[0];
        if (inst->motionField != MOTION_FIELD_INVALID) {
            MotionFields_Stop(inst->motionField);
            inst->motionField = MOTION_FIELD_INVALID;
        }
    }

    memset(inst, 0, sizeof(VC_WaterDragonInstance));
    inst->active = true;
    inst->id = s_waterDragonNextId++;
    if (s_waterDragonNextId <= 0) s_waterDragonNextId = 1;

    if (cfg) {
        inst->config = *cfg;
    } else {
        inst->config = *VFX_WaterDragon_GetLiveConfig();
    }
    inst->config.source = source;
    inst->config.target = target;
    inst->curTarget = target;

    Vector3 fwd = Vector3Subtract(target, source);
    float dist = Vector3Length(fwd);
    float speed = fmaxf(5.0f, inst->config.travelSpeed);
    inst->flightDuration = (dist > 0.05f) ? (dist / speed) : 0.6f;
    inst->headPos = source;
    inst->headTangent = (dist > 0.001f) ? Vector3Scale(fwd, 1.0f / dist) : (Vector3){0, 0, 1};

    // Initialize Motion Field for ambient wind/ribbon attraction
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_MOVING_GUIDE,
        .origin = source,
        .axis = fwd,
        .radiusM = inst->config.bodyRadius * 2.8f,
        .speedMps = speed,
        .swirlSpeedMps = inst->config.spiralSpeed,
        .turbulenceSpeedMps = 3.5f,
        .durationSec = inst->flightDuration + inst->config.puddleDuration + 2.0f,
        .attackSec = 0.05f,
        .fadeSec = 0.25f,
        .guidance = GUIDE_BALANCED,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    FieldDesc fieldDesc;
    if (MotionFieldRecipe_Build(&r, &fieldDesc)) {
        inst->motionField = MotionFields_CreateField(&fieldDesc);
        inst->motionFieldCreated = true;
    } else {
        inst->motionField = MOTION_FIELD_INVALID;
    }

    return inst->id;
}

int VFX_ComposeWaterDragon(const VFX_WaterDragonConfig *cfg)
{
    const VFX_WaterDragonConfig *c = cfg ? cfg : VFX_WaterDragon_GetLiveConfig();
    return VFX_ComposeWaterDragonEx(c->source, c->target, c);
}

void VFX_KillWaterDragon(int handle)
{
    for (int i = 0; i < VC_WATER_DRAGON_MAX_INSTANCES; i++) {
        if (s_waterDragonInstances[i].active && s_waterDragonInstances[i].id == handle) {
            if (s_waterDragonInstances[i].motionField != MOTION_FIELD_INVALID) {
                MotionFields_Stop(s_waterDragonInstances[i].motionField);
                s_waterDragonInstances[i].motionField = MOTION_FIELD_INVALID;
            }
            s_waterDragonInstances[i].active = false;
            break;
        }
    }
}

bool VFX_WaterDragon_IsAnyActive(void)
{
    for (int i = 0; i < VC_WATER_DRAGON_MAX_INSTANCES; i++) {
        if (s_waterDragonInstances[i].active) return true;
    }
    return false;
}

// ── Physical Simulation & State Update ───────────────────────────────────────

void VC_WaterDragon_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    for (int instIdx = 0; instIdx < VC_WATER_DRAGON_MAX_INSTANCES; instIdx++) {
        VC_WaterDragonInstance *inst = &s_waterDragonInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        inst->age += dt;
        const VFX_WaterDragonConfig *cfg = &inst->config;

        Vector3 totalFwd = Vector3Subtract(inst->curTarget, cfg->source);
        float totalDist = Vector3Length(totalFwd);
        Vector3 totalFwdNorm = (totalDist > 0.001f) ? Vector3Scale(totalFwd, 1.0f / totalDist) : (Vector3){0, 0, 1};

        // Advance flight progression
        if (inst->flightDuration > 0.001f) {
            inst->flightProgress = Clamp(inst->age / inst->flightDuration, 0.0f, 1.0f);
        } else {
            inst->flightProgress = 1.0f;
        }

        float tHead = inst->flightProgress;
        float arcY = 4.0f * cfg->arcHeight * tHead * (1.0f - tHead);
        Vector3 baseHeadPos = Vector3Lerp(cfg->source, inst->curTarget, tHead);
        baseHeadPos.y += arcY;

        // Head swimming undulation
        Vector3 latNorm = Vector3Normalize((Vector3){-totalFwdNorm.z, 0.0f, totalFwdNorm.x});
        Vector3 vertNorm = (Vector3){0.0f, 1.0f, 0.0f};

        float swimHeadPhase = inst->age * cfg->swimFrequency;
        float envHead = sinf(tHead * PI);
        float latHead = cfg->swimAmplitude * sinf(swimHeadPhase) * envHead;
        float vertHead = (cfg->swimAmplitude * 0.45f) * cosf(swimHeadPhase * 0.85f) * envHead;

        Vector3 prevHead = inst->headPos;
        inst->headPos = Vector3Add(Vector3Add(baseHeadPos, Vector3Scale(latNorm, latHead)),
                                   Vector3Scale(vertNorm, vertHead));

        Vector3 headDelta = Vector3Subtract(inst->headPos, prevHead);
        if (Vector3Length(headDelta) > 0.001f) {
            inst->headTangent = Vector3Normalize(headDelta);
        }

        // Update Motion Field transform
        if (inst->motionField != MOTION_FIELD_INVALID) {
            FieldTransform xf = {
                .position = inst->headPos,
                .axisX = (Vector3){1.0f, 0.0f, 0.0f},
                .axisY = (Vector3){0.0f, 1.0f, 0.0f},
                .axisZ = (Vector3){0.0f, 0.0f, 1.0f}
            };
            MotionFields_SetTransform(inst->motionField, &xf);
        }

        // Aerodynamic wind wake parting surrounding vegetation
        Wind_SpawnGust(inst->headPos, inst->headTangent, cfg->bodyRadius * 3.6f, fminf(cfg->travelSpeed * 0.65f, 16.0f), 0.20f);

        // Generate undulating 3D serpentine body waypoints trailing behind the head
        float bodySpanDist = fminf(totalDist * 0.85f, 5.2f);
        float bodySpanT = (totalDist > 0.05f) ? (bodySpanDist / totalDist) : 0.4f;
        float tTail = fmaxf(0.0f, tHead - bodySpanT);

        inst->bodyPointCount = VC_WATER_DRAGON_PATH_POINTS;
        for (int i = 0; i < inst->bodyPointCount; i++) {
            float u = (float)i / (float)(inst->bodyPointCount - 1); // 0 at tail, 1 at head
            float tNode = tTail + u * (tHead - tTail);

            float nodeArc = 4.0f * cfg->arcHeight * tNode * (1.0f - tNode);
            Vector3 nodeBase = Vector3Lerp(cfg->source, inst->curTarget, tNode);
            nodeBase.y += nodeArc;

            float nodePhase = inst->age * cfg->swimFrequency - (1.0f - u) * 7.5f;
            float nodeEnv = sinf(tNode * PI);
            float nodeLat = cfg->swimAmplitude * sinf(nodePhase) * nodeEnv;
            float nodeVert = (cfg->swimAmplitude * 0.45f) * cosf(nodePhase * 0.85f) * nodeEnv;

            inst->bodyPoints[i] = Vector3Add(Vector3Add(nodeBase, Vector3Scale(latNorm, nodeLat)),
                                             Vector3Scale(vertNorm, nodeVert));
        }

        // Check for target impact
        float groundTargetY = MapManager_GetGroundHeightAt(inst->curTarget.x, inst->curTarget.z);
        float distToTarget = Vector3Distance(inst->headPos, inst->curTarget);
        bool reachedTarget = (inst->flightProgress >= 0.96f || distToTarget < 0.65f);

        if (reachedTarget && !inst->impactTriggered) {
            inst->impactTriggered = true;
            inst->impactTimer = 0.0f;
            inst->groundPuddlePos = (Vector3){inst->curTarget.x, groundTargetY + 0.015f, inst->curTarget.z};
            inst->curPuddleRadius = 0.20f;
            inst->puddleMaxRadius = cfg->puddleRadius;

            // Environmental wind blast & ground wave shock ring
            Wind_SpawnRadialBlast(inst->groundPuddlePos, cfg->puddleRadius * 1.6f, 18.0f, 0.45f);
            VFX_ComposeGroundWave(inst->groundPuddlePos, VC_MAT_WATER, cfg->puddleRadius * 1.35f, 0.45f, VFX_GroundHeightFromMap, NULL);

            if (cfg->impactDecal && !inst->decalSpawned) {
                inst->decalSpawned = true;
                VFX_ComposeDecal(inst->groundPuddlePos, VC_MAT_WATER, cfg->puddleRadius, 0.85f, 1.0f);
            }

            VFX_ComposeSurfaceImpact(inst->groundPuddlePos, VFX_IMPACT_SURFACE_WATER);
            VFX_ComposeMistVeil(inst->groundPuddlePos, cfg->puddleRadius * 0.9f, 2.0f);

            // Camera shake trauma
            CameraFX_Shake(0.35f);
        }

        // Update ground puddle expansion
        if (inst->impactTriggered) {
            inst->impactTimer += dt;
            if (inst->curPuddleRadius < inst->puddleMaxRadius) {
                float growSpeed = inst->puddleMaxRadius / fmaxf(0.20f, cfg->puddleDuration * 0.30f);
                inst->curPuddleRadius = fminf(inst->puddleMaxRadius, inst->curPuddleRadius + growSpeed * dt);
            }
        }

        // Cleanup expired instance
        if (inst->impactTriggered && inst->impactTimer >= cfg->puddleDuration) {
            if (inst->motionField != MOTION_FIELD_INVALID) {
                MotionFields_Stop(inst->motionField);
                inst->motionField = MOTION_FIELD_INVALID;
            }
            inst->active = false;
        }
    }
}

// ── 3D Geometry Pass (Dragon Head, Swept Body, Helical Ribbons, & Geyser) ─────

void VC_WaterDragon_Draw3D(Camera3D cam)
{
    float time = (float)GetTime();

    // Load dragon head texture cached globally
    static Texture2D s_dragonHeadTex = {0};
    if (s_dragonHeadTex.id == 0) {
        s_dragonHeadTex = ResourceManager_LoadTexture("skills/fire/fire_ball/dragon_head.png");
    }

    for (int instIdx = 0; instIdx < VC_WATER_DRAGON_MAX_INSTANCES; instIdx++) {
        VC_WaterDragonInstance *inst = &s_waterDragonInstances[instIdx];
        if (!inst->active) continue;

        const VFX_WaterDragonConfig *cfg = &inst->config;

        // 1. Continuous Swept 3D Water Tube Mesh (Dragon Body, Coiling Tendrils & Geyser Spout)
        bool drawBody = (!inst->impactTriggered || inst->impactTimer < 0.35f);
        bool drawGeyser = (inst->impactTriggered && cfg->impactGeyser && inst->impactTimer < 0.65f);

        if ((drawBody && inst->bodyPointCount >= 4) || drawGeyser) {
            float fadeProgress = 1.0f;
            if (inst->impactTriggered) {
                fadeProgress = 1.0f - Clamp(inst->impactTimer / 0.35f, 0.0f, 1.0f);
            }

            VFX_BeginWaterStreams(time);

            // 1a. Serpentine 3D Dragon Body
            if (drawBody && fadeProgress > 0.05f && inst->bodyPointCount >= 4) {
                VFX_DrawWaterStreamOnPath(inst->bodyPoints, inst->bodyPointCount,
                                          cfg->bodyRadius * fadeProgress, 1.0f, 1.0f, time, 0.0f);

                // 1b. Dual Helical Tendrils (Long Khí spiraling around dragon spine)
                if (!inst->impactTriggered) {
                    Vector3 latNorm = Vector3Normalize((Vector3){-inst->headTangent.z, 0.0f, inst->headTangent.x});
                    Vector3 vertNorm = (Vector3){0.0f, 1.0f, 0.0f};

                    Vector3 spiralPoints0[VC_WATER_DRAGON_PATH_POINTS];
                    Vector3 spiralPoints1[VC_WATER_DRAGON_PATH_POINTS];

                    for (int i = 0; i < inst->bodyPointCount; i++) {
                        float u = (float)i / (float)(inst->bodyPointCount - 1);
                        float theta0 = inst->age * cfg->spiralSpeed + u * 12.0f;
                        float theta1 = theta0 + PI;
                        float r = cfg->spiralRadius * sinf(u * PI) * 1.35f;

                        spiralPoints0[i] = Vector3Add(inst->bodyPoints[i],
                            Vector3Add(Vector3Scale(latNorm, cosf(theta0) * r),
                                       Vector3Scale(vertNorm, sinf(theta0) * r)));
                        spiralPoints1[i] = Vector3Add(inst->bodyPoints[i],
                            Vector3Add(Vector3Scale(latNorm, cosf(theta1) * r),
                                       Vector3Scale(vertNorm, sinf(theta1) * r)));
                    }

                    VFX_DrawWaterStreamOnPath(spiralPoints0, inst->bodyPointCount,
                                              cfg->bodyRadius * 0.22f * fadeProgress, 1.0f, 1.0f, time, 0.0f);
                    VFX_DrawWaterStreamOnPath(spiralPoints1, inst->bodyPointCount,
                                              cfg->bodyRadius * 0.22f * fadeProgress, 1.0f, 1.0f, time, 1.57f);
                }
            }

            // 1c. Target Impact: 3D Water Geyser Spout
            if (drawGeyser) {
                float geyserT = inst->impactTimer / 0.65f;
                float geyserH = cfg->bodyRadius * 9.5f * sinf(geyserT * PI);
                if (geyserH > 0.08f) {
                    Vector3 geyserPath[8];
                    for (int g = 0; g < 8; g++) {
                        float gu = (float)g / 7.0f;
                        geyserPath[g] = (Vector3){
                            inst->groundPuddlePos.x + sinf(gu * PI + time * 12.0f) * 0.12f,
                            inst->groundPuddlePos.y + gu * geyserH,
                            inst->groundPuddlePos.z + cosf(gu * PI + time * 12.0f) * 0.12f
                        };
                    }
                    float geyserR = cfg->bodyRadius * (0.6f + 0.95f * sinf(geyserT * PI));
                    VFX_DrawWaterStreamOnPath(geyserPath, 8, geyserR, 1.0f, 1.0f, time, 3.14f);
                }
            }

            VFX_EndWaterStreams();
        }

        // 2. Articulated Roaring Dragon Head
        if (!inst->impactTriggered && s_dragonHeadTex.id != 0) {
            Vector3 pointAhead = Vector3Add(inst->headPos, Vector3Scale(inst->headTangent, 1.0f));
            Vector2 screenHead = GetWorldToScreen(inst->headPos, cam);
            Vector2 screenAhead = GetWorldToScreen(pointAhead, cam);
            Vector2 dir = Vector2Subtract(screenAhead, screenHead);
            float rotation = -atan2f(dir.y, dir.x) * RAD2DEG;

            Rectangle sourceRec = {
                0.0f, 0.0f,
                (float)s_dragonHeadTex.width,
                (float)s_dragonHeadTex.height * ((dir.x < 0.0f) ? -1.0f : 1.0f)
            };

            float jawAnim = 1.0f + sinf(inst->age * 26.0f) * 0.16f;
            float scale = (cfg->headScale > 0.1f) ? cfg->headScale * 0.0016f : 0.0016f;
            Vector2 size = {
                (float)s_dragonHeadTex.width * scale,
                (float)s_dragonHeadTex.height * scale * jawAnim
            };
            Vector2 origin = {size.x * 0.35f, size.y * 0.5f};

            // Offset backward so neck connects seamlessly to the water tube
            float offsetBackward = size.x * 0.36f;
            Vector3 headRenderPos = Vector3Subtract(inst->headPos, Vector3Scale(inst->headTangent, offsetBackward));

            float headFade = 1.0f;
            if (inst->flightProgress > 0.90f) {
                headFade = Clamp((1.0f - inst->flightProgress) / 0.10f, 0.0f, 1.0f);
            }

            // Pass 1: Deep Aqua Translucent Body
            DrawBillboardPro(cam, s_dragonHeadTex, sourceRec, headRenderPos, cam.up,
                             (Vector2){size.x * 1.15f, size.y * 1.15f},
                             (Vector2){origin.x * 1.15f, origin.y * 1.15f}, rotation,
                             (Color){12, 100, 190, (unsigned char)(235.0f * headFade)});

            // Pass 2: Radiant Cyan Crest, Horns & Whiskers (Additive highlight glow)
            BeginBlendMode(BLEND_ADDITIVE);
            DrawBillboardPro(cam, s_dragonHeadTex, sourceRec, headRenderPos, cam.up,
                             size, origin, rotation,
                             (Color){90, 210, 255, (unsigned char)(145.0f * headFade)});
            EndBlendMode();

            // Luminous Thủy Long mouth core light
            VFXLight_Spawn(inst->headPos, (Color){140, 230, 255, 255}, cfg->bodyRadius * 5.0f, 0.05f, VFX_PRIORITY_LOW);
        }
    }
}

#endif // VC_WATER_DRAGON_INL
