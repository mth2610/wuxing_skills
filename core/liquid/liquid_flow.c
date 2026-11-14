#include "core/liquid/liquid_flow.h"

#include "core/liquid/liquid_surface.h"
#include "core/map_manager.h"
#include "core/motion/analytic_flow.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

#define LIQUID_FLOW_MAX_INSTANCES 8

typedef struct {
    bool active;
    LiquidFlowPattern pattern;
    Vector3 origin;
    Vector3 target;
    float radius;
    float speed;
    float arcHeight;
    float age;
    float travelTime;
    float duration;
    int parcelCount;
    bool sampleTerrain;
    LiquidDesc material;
} LiquidFlowInstance;

static LiquidFlowInstance s_instances[LIQUID_FLOW_MAX_INSTANCES];
static AnalyticFlowSample s_samples[ANALYTIC_FLOW_MAX_SAMPLES];

static bool Flow_ColorUnset(Color c) { return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0; }

static AnalyticFlowPattern Flow_ToAnalyticPattern(LiquidFlowPattern p)
{
    switch (p) {
    case LIQUID_FLOW_DRAGON_SURGE: return ANALYTIC_FLOW_DRAGON_SURGE;
    case LIQUID_FLOW_BLADE_SLASH:  return ANALYTIC_FLOW_BLADE_SLASH;
    case LIQUID_FLOW_VORTEX_SURGE: return ANALYTIC_FLOW_VORTEX_SURGE;
    case LIQUID_FLOW_ORB:
    default:                       return ANALYTIC_FLOW_ORB;
    }
}

static AnalyticFlowDesc Flow_MakeAnalyticDesc(const LiquidFlowInstance *inst)
{
    AnalyticFlowDesc d = {
        .pattern = Flow_ToAnalyticPattern(inst->pattern),
        .origin = inst->origin,
        .target = inst->target,
        .radius = inst->radius,
        .speed = inst->speed,
        .arcHeight = inst->arcHeight,
        .duration = inst->duration,
        .sampleCount = inst->parcelCount
    };
    return d;
}

void LiquidFlow_Init(void)
{
    AnalyticFlow_InitSeeds();
}

void LiquidFlow_Unload(void)
{
    memset(s_instances, 0, sizeof(s_instances));
}

int LiquidFlow_Spawn(const LiquidFlowDesc *desc)
{
    if (!desc) return -1;
    AnalyticFlow_InitSeeds();

    int slot = -1;
    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        if (!s_instances[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    LiquidFlowInstance *inst = &s_instances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->pattern = desc->pattern;
    inst->origin = desc->origin;
    inst->target = desc->target;
    inst->radius = desc->radius > 0.05f ? desc->radius : 0.42f;
    inst->speed = desc->speed > 0.5f ? desc->speed : 14.0f;
    inst->arcHeight = desc->arcHeight >= 0.0f ? desc->arcHeight : 1.6f;
    inst->sampleTerrain = desc->sampleTerrain;

    int pCount = desc->parcelCount > 0 ? desc->parcelCount : 220;
    if (pCount > ANALYTIC_FLOW_MAX_SAMPLES) pCount = ANALYTIC_FLOW_MAX_SAMPLES;
    inst->parcelCount = pCount;

    AnalyticFlowDesc ad = Flow_MakeAnalyticDesc(inst);
    inst->travelTime = AnalyticFlow_TravelTime(&ad);
    inst->duration = (desc->duration > 0.1f) ? desc->duration : AnalyticFlow_DefaultDuration(&ad);

    Color body = Flow_ColorUnset(desc->bodyColor) ? (Color){32, 152, 148, 255} : desc->bodyColor;
    Color glow = Flow_ColorUnset(desc->glowColor) ? (Color){36, 189, 173, 255} : desc->glowColor;
    Color soft = Flow_ColorUnset(desc->softColor) ? (Color){168, 232, 224, 255} : desc->softColor;
    inst->material = LiquidSurface_DielectricDesc(body, glow, soft);
    inst->material.ior = 1.333f;
    inst->material.roughnessScale = 0.35f;
    inst->material.opacityPerMetre = 0.32f;
    inst->material.foam = 1.0f;
    return slot;
}

void LiquidFlow_Update(float dt)
{
    if (dt <= 0.0f) return;
    float step = fminf(dt, 0.05f);
    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        LiquidFlowInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        inst->age += step;
        if (inst->age >= inst->duration) {
            inst->active = false;
        }
    }
}

static float Flow_GroundY(const LiquidFlowInstance *inst, float x, float z, float fallbackY)
{
    return inst->sampleTerrain ? MapManager_GetGroundHeightAt(x, z) : fallbackY;
}

static void Flow_EmitCrownSplash(const LiquidFlowInstance *inst, float splashAge, int count, float scale)
{
    if (splashAge <= 0.0f || count <= 0) return;
    float groundY = Flow_GroundY(inst, inst->target.x, inst->target.z, inst->target.y);
    Vector3 center = {inst->target.x, groundY, inst->target.z};

    int n = AnalyticFlow_EvaluateCrownSplash(center, splashAge, scale, count,
                                             s_samples, ANALYTIC_FLOW_MAX_SAMPLES);
    for (int i = 0; i < n; ++i) {
        const AnalyticFlowSample *s = &s_samples[i];
        if (!s->active) continue;

        float gnd = Flow_GroundY(inst, s->position.x, s->position.z, groundY);
        if (s->position.y < gnd + 0.05f) {
            float r = s->radius * 1.18f;
            if (r > 0.015f) {
                LiquidSurface_RegisterEllipsoid((Vector3){s->position.x, gnd + 0.03f, s->position.z},
                    (Vector3){r * 1.45f, r * 0.45f, r * 1.45f});
            }
        } else if (s->radius > 0.015f) {
            LiquidSurface_RegisterParticle(s->position, s->radius);
        }
    }
}

void LiquidFlow_DrawSSF(void)
{
    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        const LiquidFlowInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        LiquidSurface_BindMaterial(&inst->material);
        LiquidSurface_HintBody(Vector3Lerp(inst->origin, inst->target, 0.5f),
                               fmaxf(inst->radius * 2.2f, 1.5f));
        LiquidSurface_SetReconstructionRadiusFor(LIQUID_PRIORITY_CAST,
                                                 fmaxf(inst->radius * 0.28f, 0.09f));

        AnalyticFlowDesc ad = Flow_MakeAnalyticDesc(inst);
        if (inst->pattern == LIQUID_FLOW_VORTEX_SURGE && inst->sampleTerrain) {
            ad.target.y = Flow_GroundY(inst, inst->target.x, inst->target.z, inst->target.y);
        }

        if (inst->pattern == LIQUID_FLOW_ORB && inst->age >= inst->travelTime) {
            Flow_EmitCrownSplash(inst, inst->age - inst->travelTime, inst->parcelCount,
                                 fmaxf(inst->radius * 2.1f, 0.85f));
            continue;
        }

        int n = AnalyticFlow_Evaluate(&ad, inst->age, s_samples, ANALYTIC_FLOW_MAX_SAMPLES);
        for (int k = 0; k < n; ++k) {
            const AnalyticFlowSample *s = &s_samples[k];
            if (!s->active) continue;

            Vector3 p = s->position;
            if (inst->pattern == LIQUID_FLOW_DRAGON_SURGE && inst->sampleTerrain) {
                p.y = fmaxf(Flow_GroundY(inst, p.x, p.z, p.y) + 0.12f, p.y);
            }
            LiquidSurface_RegisterParticle(p, s->radius);
        }

        if (inst->pattern == LIQUID_FLOW_DRAGON_SURGE && inst->age >= inst->travelTime * 0.88f) {
            int bodyCount = (int)((float)inst->parcelCount * 0.68f);
            Flow_EmitCrownSplash(inst, inst->age - inst->travelTime * 0.88f,
                                 inst->parcelCount - bodyCount,
                                 fmaxf(inst->radius * 2.2f, 0.95f));
        }
    }
}

int LiquidFlow_GetActiveCount(void)
{
    int count = 0;
    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        if (s_instances[i].active) count++;
    }
    return count;
}

static Vector3 Flow_ApproxCenter(const LiquidFlowInstance *inst, float *outRadius)
{
    float u = Clamp(inst->age / fmaxf(inst->travelTime, 0.1f), 0.0f, 1.0f);
    if (inst->pattern == LIQUID_FLOW_VORTEX_SURGE) {
        *outRadius = fmaxf(inst->radius * 1.4f, 0.7f);
        return inst->target;
    }
    if (inst->pattern == LIQUID_FLOW_BLADE_SLASH) {
        *outRadius = fmaxf(inst->radius * 1.6f, 0.8f);
        return inst->origin;
    }
    Vector3 p = Vector3Lerp(inst->origin, inst->target, u);
    p.y += sinf(u * PI) * inst->arcHeight;
    *outRadius = (inst->age >= inst->travelTime) ? fmaxf(inst->radius * 1.6f, 0.75f) : inst->radius;
    return p;
}

void LiquidFlow_DrawSmartShadows(void)
{
    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        const LiquidFlowInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        float rad = 0.4f;
        Vector3 p = Flow_ApproxCenter(inst, &rad);
        float groundY = Flow_GroundY(inst, p.x, p.z, p.y);
        float height = fmaxf(0.0f, p.y - groundY);
        float fade = 1.0f - Clamp((height - 1.5f) / 2.5f, 0.0f, 1.0f);
        if (fade <= 0.05f) continue;

        float w = rad * (1.15f + height * 0.12f) * fade;
        Environment_DrawSmartShadow((Vector3){p.x, groundY + 0.02f, p.z},
                                    ENV_SHAPE_SPHERE, w, rad * 0.9f * fade);
    }
}

void LiquidFlow_DrawShadowPass(void)
{
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0) return;

    static Mesh s_shadowSphere = {0};
    static Material s_shadowMat = {0};
    static bool s_shadowReady = false;
    if (!s_shadowReady) {
        s_shadowSphere = GenMeshSphere(1.0f, 10, 8);
        s_shadowMat = LoadMaterialDefault();
        s_shadowReady = true;
    }
    s_shadowMat.shader = depthShader;

    for (int i = 0; i < LIQUID_FLOW_MAX_INSTANCES; ++i) {
        const LiquidFlowInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        float rad = 0.4f;
        Vector3 p = Flow_ApproxCenter(inst, &rad);
        float groundY = Flow_GroundY(inst, p.x, p.z, p.y);
        float fade = 1.0f - Clamp(((p.y - groundY) - 1.5f) / 2.2f, 0.0f, 1.0f);
        float r = rad * 0.85f * fade;
        if (r <= 0.05f) continue;

        Matrix xf = MatrixIdentity();
        xf.m0 = r; xf.m5 = r; xf.m10 = r;
        xf.m12 = p.x; xf.m13 = p.y; xf.m14 = p.z;
        DrawMesh(s_shadowSphere, s_shadowMat, xf);
    }
}
