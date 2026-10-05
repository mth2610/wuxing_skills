#ifndef VC_WOOD_LEAVES_INL
#define VC_WOOD_LEAVES_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include <math.h>

VFX_WoodLeavesConfig VFX_WoodLeaves_DefaultConfig(void)
{
    VFX_WoodLeavesConfig cfg;
    cfg.attached = false;
    cfg.origin = (Vector3){0.0f, 3.2f, 0.0f};
    cfg.radius = 1.5f;
    cfg.count = 64;
    cfg.mass = 0.004f;
    cfg.initialVelocity = (Vector3){0.0f, 0.6f, 0.0f};
    cfg.velocitySpread = 1.2f;
    cfg.sockets = NULL;
    cfg.socketCount = 0;
    cfg.growth = 1.0f;
    cfg.wither = 0.0f;
    cfg.swayAmp = 0.035f;
    cfg.size = 0.16f;
    cfg.shape = WOOD_LEAF_SHAPE_OVAL;
    cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
    cfg.seed = 12345;
    return cfg;
}

const char* VFX_WoodLeafShape_Name(VFX_WoodLeafShape shape)
{
    switch (shape)
    {
        case WOOD_LEAF_SHAPE_OVAL:   return "OVAL BROADLEAF";
        case WOOD_LEAF_SHAPE_WILLOW: return "WILLOW TENDRIL";
        case WOOD_LEAF_SHAPE_MAPLE:  return "BRAMBLE SERRATED";
        default:                     return "UNKNOWN LEAF";
    }
}

static const char *s_woodLeafShapeDisplayNames[WOOD_LEAF_SHAPE_COUNT] = {
    "OVAL BROADLEAF", "WILLOW TENDRIL", "BRAMBLE SERRATED"
};

static const char *s_woodLeafStyleDisplayNames[WOOD_VINE_STYLE_COUNT] = {
    "JADE EMERALD", "BLOOD BRAMBLE", "GOLDEN AMBER", "TAICHI INK"
};

int VFX_WoodLeaves_GetParams(VFX_WoodLeavesConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Shape", .group = "Leaves", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->shape, .minInt = 0, .maxInt = WOOD_LEAF_SHAPE_COUNT - 1,
            .enumNames = s_woodLeafShapeDisplayNames, .enumCount = WOOD_LEAF_SHAPE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Style", .group = "Leaves", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->style, .minInt = 0, .maxInt = WOOD_VINE_STYLE_COUNT - 1,
            .enumNames = s_woodLeafStyleDisplayNames, .enumCount = WOOD_VINE_STYLE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Attached Mode", .group = "Leaves", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->attached
        };
    }
    return n;
}

/* Renders natural instanced foliage leaves on botanical sockets (ATTACHED)
 * or emits physical airborne leaves drifting in wind & gravity (FREE). */
void VFX_ComposeWoodLeaves(const VFX_WoodLeavesConfig *config)
{
    if (config == NULL || config->growth <= 0.01f)
        return;

    // ── Mode B: Standalone / Free Airborne Leaves (Wind, Gravity & Air Drag) ──
    if (!config->attached || config->sockets == NULL || config->socketCount <= 0)
    {
        static float s_freeLeafTimer = 0.0f;
        s_freeLeafTimer += GetFrameTime();
        int targetCount = config->count > 0 ? config->count : 64;

        if (s_freeLeafTimer > 1.2f || VFX_FoliageSystem_GetActiveCount() < targetCount / 3)
        {
            s_freeLeafTimer = 0.0f;
            VFX_FoliageSpawnParams sp = VFX_FoliageSpawnParams_Default();
            sp.kind = BOTANICAL_KIND_LEAF;
            sp.leafShape = config->shape;
            sp.style = config->style;
            sp.origin = config->origin;
            sp.radius = config->radius > 0.1f ? config->radius : 1.5f;
            sp.count = targetCount;
            sp.attached = false;
            sp.mass = config->mass > 1e-4f ? config->mass : 0.004f;
            sp.initialVelocity = config->initialVelocity;
            sp.velocitySpread = config->velocitySpread > 0.1f ? config->velocitySpread : 1.2f;
            sp.size = config->size > 0.02f ? config->size : 0.16f;
            sp.growth = config->growth;
            sp.lifetime = 10.0f;
            sp.seed = config->seed;
            VFX_FoliageSystem_SpawnCluster(&sp);
        }
        return;
    }

    // ── Mode A: Attached Botanical Sockets on Vine / Tree Host ────────────────
    bool isShadowPass = EnvShadow_IsCapturing();
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float time = (float)GetTime();

    // Palette setup based on style and wither
    Color leafBase, leafTip, leafVein;
    switch (config->style)
    {
        case WOOD_VINE_STYLE_BLOOD_BRAMBLE: // Cinnabar ruby vs green grass
            leafBase = (Color){150, 22, 35, 255};
            leafTip  = (Color){235, 45, 62, 255};
            leafVein = (Color){85, 12, 18, 255};
            break;
        case WOOD_VINE_STYLE_GOLDEN_AMBER:  // Liquid amber gold
            leafBase = (Color){195, 140, 32, 255};
            leafTip  = (Color){250, 205, 68, 255};
            leafVein = (Color){125, 75, 18, 255};
            break;
        case WOOD_VINE_STYLE_WITHER_GHOST:  // Spectral violet
            leafBase = (Color){115, 95, 135, 255};
            leafTip  = (Color){195, 145, 240, 255};
            leafVein = (Color){70, 55, 85, 255};
            break;
        case WOOD_VINE_STYLE_JADE_EMERALD:  // High-contrast celestial jade
        default:
            leafBase = (Color){28, 145, 68, 255};
            leafTip  = (Color){50, 240, 160, 255};
            leafVein = (Color){16, 85, 40, 255};
            break;
    }

    // Wither color shifts toward golden yellow then dried brown
    if (config->wither > 0.02f)
    {
        float w = config->wither;
        Color autumnGold = (Color){205, 155, 38, 255};
        Color deadBrown  = (Color){85, 52, 25, 255};

        float phase1 = w < 0.5f ? (w * 2.0f) : 1.0f;
        float phase2 = w > 0.5f ? ((w - 0.5f) * 2.0f) : 0.0f;

        leafBase.r = (unsigned char)(leafBase.r * (1.0f - phase1) + autumnGold.r * phase1);
        leafBase.g = (unsigned char)(leafBase.g * (1.0f - phase1) + autumnGold.g * phase1);
        leafBase.b = (unsigned char)(leafBase.b * (1.0f - phase1) + autumnGold.b * phase1);

        leafBase.r = (unsigned char)(leafBase.r * (1.0f - phase2) + deadBrown.r * phase2);
        leafBase.g = (unsigned char)(leafBase.g * (1.0f - phase2) + deadBrown.g * phase2);
        leafBase.b = (unsigned char)(leafBase.b * (1.0f - phase2) + deadBrown.b * phase2);

        leafTip = leafBase;
    }

    rlDisableBackfaceCulling();
    if (!isShadowPass) BeginBlendMode(BLEND_ALPHA);

    // Each leaf has 4 triangles (12 vertices), 2-sided = 24 vertices
    rlCheckRenderBatchLimit(config->socketCount * 24);
    rlBegin(RL_TRIANGLES);

    for (int k = 0; k < config->socketCount; k++)
    {
        const VFX_BotanicalSocket *sock = &config->sockets[k];

        // 1. Biological emergence progress for this leaf
        float leafBirth = sock->arc * 0.65f;
        float localGrowth = (config->growth - leafBirth) / 0.35f;
        localGrowth = (localGrowth < 0.0f) ? 0.0f : (localGrowth > 1.0f ? 1.0f : localGrowth);
        if (localGrowth <= 0.01f) continue;

        // Smooth growth curve with organic unfurl overshoot bounce
        float tGrow = localGrowth * localGrowth * (3.0f - 2.0f * localGrowth);
        float popBounce = 1.0f + 0.16f * sinf(localGrowth * PI) * (1.0f - localGrowth);
        float leafLen = (config->size > 0.04f ? config->size : 0.16f) * tGrow * popBounce;

        // Desiccation shrinkage under wither
        if (config->wither > 0.1f) {
            leafLen *= (1.0f - config->wither * 0.35f);
        }

        // 2. Leaf blade orientation from socket frame
        Vector3 stemDir  = sock->normal;
        Vector3 spineDir = sock->tangent;

        // Leaf petiole (cuống lá) extends outward
        float stemLen = sock->stemRadius * 0.65f + 0.02f;
        Vector3 bladeBase = Vector3Add(sock->pos, Vector3Scale(stemDir, stemLen));

        // Leaf growth direction: tilted outward and forward along branch
        Vector3 leafDir = Vector3Normalize(Vector3Add(Vector3Scale(stemDir, 0.60f), Vector3Scale(spineDir, 0.45f)));
        Vector3 leafNorm = Vector3Normalize(Vector3CrossProduct(stemDir, spineDir));
        Vector3 leafSide = Vector3Normalize(Vector3CrossProduct(leafDir, leafNorm));

        // High frequency biological wind flutter
        float flutterPhase = time * 7.5f + (float)k * 1.8f;
        float sway = (config->swayAmp > 0.001f ? config->swayAmp : 0.035f);
        float flutter = sinf(flutterPhase) * sway * (0.5f + 0.5f * sock->arc);
        Vector3 flutterVec = Vector3Scale(leafNorm, flutter);

        // Blade aspect ratio based on shape
        float widthFrac = 0.38f;
        if (config->shape == WOOD_LEAF_SHAPE_WILLOW) widthFrac = 0.18f;
        else if (config->shape == WOOD_LEAF_SHAPE_MAPLE) widthFrac = 0.52f;
        float leafWidth = leafLen * widthFrac;

        // 3. Build 3D V-creased curved leaf blade vertices
        Vector3 vRoot = bladeBase;
        Vector3 vMid  = Vector3Add(bladeBase, Vector3Add(Vector3Scale(leafDir, leafLen * 0.50f),
                                                         Vector3Add(Vector3Scale(stemDir, leafLen * 0.08f), Vector3Scale(flutterVec, 0.4f))));
        Vector3 vTip  = Vector3Add(bladeBase, Vector3Add(Vector3Scale(leafDir, leafLen * 1.05f),
                                                         Vector3Add(Vector3Scale(stemDir, -leafLen * 0.04f), flutterVec)));

        // V-crease upward fold
        Vector3 vFold = Vector3Scale(leafNorm, leafLen * 0.065f);
        Vector3 vLeft = Vector3Add(bladeBase, Vector3Add(Vector3Scale(leafDir, leafLen * 0.42f),
                                                         Vector3Add(Vector3Scale(leafSide, -leafWidth), vFold)));
        Vector3 vRight = Vector3Add(bladeBase, Vector3Add(Vector3Scale(leafDir, leafLen * 0.42f),
                                                          Vector3Add(Vector3Scale(leafSide, leafWidth), vFold)));

        // Compute face normals for 3D creased lighting
        Vector3 nL1 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vLeft, vRoot), Vector3Subtract(vMid, vRoot)));
        Vector3 nR1 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vMid, vRoot), Vector3Subtract(vRight, vRoot)));
        Vector3 nL2 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vLeft, vMid), Vector3Subtract(vTip, vMid)));
        Vector3 nR2 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vTip, vMid), Vector3Subtract(vRight, vMid)));

        if (!isShadowPass)
        {
            // Two-sided subsurface transmission + diffuse sunlight
            float diffL1 = 0.35f + 0.65f * fmaxf(Vector3DotProduct(nL1, sunDir), 0.0f) + 0.30f * fmaxf(-Vector3DotProduct(nL1, sunDir), 0.0f);
            float diffR1 = 0.35f + 0.65f * fmaxf(Vector3DotProduct(nR1, sunDir), 0.0f) + 0.30f * fmaxf(-Vector3DotProduct(nR1, sunDir), 0.0f);
            float diffL2 = 0.35f + 0.65f * fmaxf(Vector3DotProduct(nL2, sunDir), 0.0f) + 0.30f * fmaxf(-Vector3DotProduct(nL2, sunDir), 0.0f);
            float diffR2 = 0.35f + 0.65f * fmaxf(Vector3DotProduct(nR2, sunDir), 0.0f) + 0.30f * fmaxf(-Vector3DotProduct(nR2, sunDir), 0.0f);

            Color cV = leafVein;
            Color cB = leafBase;
            Color cT = leafTip;

            // Front faces
            // Left base quad/tri
            rlColor4ub((unsigned char)(cV.r * diffL1), (unsigned char)(cV.g * diffL1), (unsigned char)(cV.b * diffL1), 255);
            rlNormal3f(nL1.x, nL1.y, nL1.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cB.r * diffL1), (unsigned char)(cB.g * diffL1), (unsigned char)(cB.b * diffL1), 255);
            rlNormal3f(nL1.x, nL1.y, nL1.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
            rlColor4ub((unsigned char)(cB.r * diffL1), (unsigned char)(cB.g * diffL1), (unsigned char)(cB.b * diffL1), 255);
            rlNormal3f(nL1.x, nL1.y, nL1.z); rlVertex3f(vMid.x, vMid.y, vMid.z);

            // Right base
            rlColor4ub((unsigned char)(cV.r * diffR1), (unsigned char)(cV.g * diffR1), (unsigned char)(cV.b * diffR1), 255);
            rlNormal3f(nR1.x, nR1.y, nR1.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
            rlColor4ub((unsigned char)(cB.r * diffR1), (unsigned char)(cB.g * diffR1), (unsigned char)(cB.b * diffR1), 255);
            rlNormal3f(nR1.x, nR1.y, nR1.z); rlVertex3f(vMid.x, vMid.y, vMid.z);
            rlColor4ub((unsigned char)(cB.r * diffR1), (unsigned char)(cB.g * diffR1), (unsigned char)(cB.b * diffR1), 255);
            rlNormal3f(nR1.x, nR1.y, nR1.z); rlVertex3f(vRight.x, vRight.y, vRight.z);

            // Left tip
            rlColor4ub((unsigned char)(cB.r * diffL2), (unsigned char)(cB.g * diffL2), (unsigned char)(cB.b * diffL2), 255);
            rlNormal3f(nL2.x, nL2.y, nL2.z); rlVertex3f(vMid.x, vMid.y, vMid.z);
            rlColor4ub((unsigned char)(cB.r * diffL2), (unsigned char)(cB.g * diffL2), (unsigned char)(cB.b * diffL2), 255);
            rlNormal3f(nL2.x, nL2.y, nL2.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
            rlColor4ub((unsigned char)(cT.r * diffL2), (unsigned char)(cT.g * diffL2), (unsigned char)(cT.b * diffL2), 255);
            rlNormal3f(nL2.x, nL2.y, nL2.z); rlVertex3f(vTip.x, vTip.y, vTip.z);

            // Right tip
            rlColor4ub((unsigned char)(cB.r * diffR2), (unsigned char)(cB.g * diffR2), (unsigned char)(cB.b * diffR2), 255);
            rlNormal3f(nR2.x, nR2.y, nR2.z); rlVertex3f(vMid.x, vMid.y, vMid.z);
            rlColor4ub((unsigned char)(cT.r * diffR2), (unsigned char)(cT.g * diffR2), (unsigned char)(cT.b * diffR2), 255);
            rlNormal3f(nR2.x, nR2.y, nR2.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
            rlColor4ub((unsigned char)(cB.r * diffR2), (unsigned char)(cB.g * diffR2), (unsigned char)(cB.b * diffR2), 255);
            rlNormal3f(nR2.x, nR2.y, nR2.z); rlVertex3f(vRight.x, vRight.y, vRight.z);
        }
        else
        {
            // Depth-only shadow map pass
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vLeft.x, vLeft.y, vLeft.z);   rlVertex3f(vMid.x, vMid.y, vMid.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vMid.x, vMid.y, vMid.z);     rlVertex3f(vRight.x, vRight.y, vRight.z);
            rlVertex3f(vMid.x, vMid.y, vMid.z);   rlVertex3f(vLeft.x, vLeft.y, vLeft.z);   rlVertex3f(vTip.x, vTip.y, vTip.z);
            rlVertex3f(vMid.x, vMid.y, vMid.z);   rlVertex3f(vTip.x, vTip.y, vTip.z);     rlVertex3f(vRight.x, vRight.y, vRight.z);
        }
    }

    rlEnd();
    if (!isShadowPass) EndBlendMode();
    rlEnableBackfaceCulling();
}

#endif // VC_WOOD_LEAVES_INL
