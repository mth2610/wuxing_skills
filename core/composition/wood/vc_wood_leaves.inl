#ifndef VC_WOOD_LEAVES_INL
#define VC_WOOD_LEAVES_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include "core/composition/wood/vc_wood_botanical_math.h"
#include <math.h>

VFX_WoodLeavesConfig VFX_WoodLeaves_DefaultConfig(void)
{
    VFX_WoodLeavesConfig cfg;
    cfg.attached = false;
    cfg.origin = (Vector3){0.0f, 3.2f, 0.0f};
    cfg.radius = 1.5f;
    cfg.count = 64;
    cfg.mass = 0; // Derived from sheet material and generated area.
    cfg.bodyMaterial = BODY_LAMINA_LEAF_FRESH;
    cfg.initialVelocity = (Vector3){0.0f, 0.6f, 0.0f};
    cfg.velocitySpread = 1.2f;
    cfg.sockets = NULL;
    cfg.socketCount = 0;
    cfg.growth = 1.0f;
    cfg.wither = 0.0f;
    cfg.swayAmp = 0.035f;
    cfg.size = 0; // Resolve representative species blade dimensions.
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

static const char *s_botanicalMaterialNames[] = {"DRY LEAF", "FRESH LEAF", "FRESH PETAL"};

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
    if (!cfg->attached && n < maxParams) {
        outParams[n++] = (VFX_ParamDef){.name="Body material", .group="Leaves",
            .type=VFX_PARAM_ENUM, .valPtr=&cfg->bodyMaterial, .minInt=0, .maxInt=2,
            .enumNames=s_botanicalMaterialNames, .enumCount=3};
    }
    return n;
}

/* Renders natural instanced foliage leaves on botanical sockets (ATTACHED)
 * or emits physical airborne leaves drifting in wind & gravity (FREE). */
void VFX_ComposeWoodLeaves(const VFX_WoodLeavesConfig *config)
{
    if (config == NULL || !isfinite(config->size) || config->growth <= 0.01f)
        return;

    VFX_BotanicalSocket fallbackSockets[4];
    const VFX_BotanicalSocket *socketsToRender = config->sockets;
    int socketCountToRender = config->socketCount;

    // ── Mode B: Standalone / Free Airborne Leaves (Wind, Gravity & Air Drag) ──
    if (!config->attached || config->sockets == NULL || config->socketCount <= 0)
    {
        for (int i = 0; i < 4; i++) {
            float ang = ((float)i / 4.0f) * 2.0f * PI + 0.35f;
            float r = 0.22f;
            fallbackSockets[i].pos = Vector3Add(config->origin, (Vector3){ cosf(ang) * r, 0.05f * (float)i, sinf(ang) * r });
            fallbackSockets[i].normal = Vector3Normalize((Vector3){ cosf(ang), 0.40f, sinf(ang) });
            fallbackSockets[i].tangent = (Vector3){ -sinf(ang), 0.6f, cosf(ang) };
            fallbackSockets[i].arc = (float)i / 3.0f;
            fallbackSockets[i].stemRadius = 0.035f;
        }
        socketsToRender = fallbackSockets;
        socketCountToRender = 4;

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
            sp.mass = config->mass > 0 ? config->mass : 0;
            sp.bodyMaterial = config->bodyMaterial;
            sp.initialVelocity = config->initialVelocity;
            sp.velocitySpread = config->velocitySpread > 0.1f ? config->velocitySpread : 1.2f;
            sp.size = Botanical_ResolveLeafSize(config->size,config->shape);
            sp.growth = config->growth;
            sp.lifetime = 10.0f;
            sp.seed = config->seed;
            VFX_FoliageSystem_SpawnCluster(&sp);
        }
    }

    // ── Mode A: Attached / Showcase Botanical Sockets ─────────────────────────
    bool isShadowPass = EnvShadow_IsCapturing();
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float time = (float)GetTime();

    // Luminous celestial palette based on style — CELESTIAL SPIRIT VEIN EMISSION
    Color leafBase, leafMid, leafTip, leafVein;
    switch (config->style)
    {
        case WOOD_VINE_STYLE_BLOOD_BRAMBLE: // Cinnabar ruby vs green grass
            leafBase = (Color){160, 22, 38, 255};
            leafMid  = (Color){240, 45, 68, 255};
            leafTip  = (Color){255, 120, 145, 255};
            leafVein = (Color){255, 210, 225, 255}; // Radiant ruby chi spine
            break;
        case WOOD_VINE_STYLE_GOLDEN_AMBER:  // Liquid amber solar gold
            leafBase = (Color){195, 140, 28, 255};
            leafMid  = (Color){250, 195, 45, 255};
            leafTip  = (Color){255, 235, 110, 255};
            leafVein = (Color){255, 252, 190, 255}; // Radiant solar gold spine
            break;
        case WOOD_VINE_STYLE_WITHER_GHOST:  // Spectral violet
            leafBase = (Color){120, 95, 145, 255};
            leafMid  = (Color){185, 135, 240, 255};
            leafTip  = (Color){220, 180, 255, 255};
            leafVein = (Color){245, 230, 255, 255}; // Spectral amethyst chi spine
            break;
        case WOOD_VINE_STYLE_JADE_EMERALD:  // High-contrast celestial jade
        default:
            leafBase = (Color){18, 145, 72, 255};
            leafMid  = (Color){45, 238, 132, 255};
            leafTip  = (Color){140, 255, 205, 255};
            leafVein = (Color){205, 255, 240, 255}; // Incandescent celestial jade spine
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
        leafBase.g = (unsigned char)(leafBase.g * (1.0f - phase2) + deadBrown.r * phase2);
        leafBase.b = (unsigned char)(leafBase.b * (1.0f - phase2) + deadBrown.r * phase2);

        leafMid  = leafBase;
        leafTip  = leafBase;
        leafVein = leafBase;
    }

    rlDisableBackfaceCulling();
    if (!isShadowPass) BeginBlendMode(BLEND_ALPHA);

    rlBegin(RL_TRIANGLES);

    for (int k = 0; k < socketCountToRender; k++)
    {
        const VFX_BotanicalSocket *sock = &socketsToRender[k];

        // 1. Biological emergence progress for this leaf
        float leafBirth = sock->arc * 0.65f;
        float localGrowth = (config->growth - leafBirth) / 0.35f;
        localGrowth = (localGrowth < 0.0f) ? 0.0f : (localGrowth > 1.0f ? 1.0f : localGrowth);
        if (localGrowth <= 0.01f) continue;

        // Smooth growth curve with organic unfurl overshoot bounce
        float tGrow = localGrowth * localGrowth * (3.0f - 2.0f * localGrowth);
        float popBounce = 1.0f + 0.16f * sinf(localGrowth * PI) * (1.0f - localGrowth);
        float leafLen = Botanical_ResolveLeafSize(config->size,config->shape) * tGrow * popBounce;

        // Desiccation shrinkage under wither
        if (config->wither > 0.1f) {
            leafLen *= (1.0f - config->wither * 0.35f);
        }

        // 2. Leaf blade orientation from socket frame
        Vector3 stemDir  = sock->normal;
        Vector3 spineDir = sock->tangent;

        // Leaf petiole (cuống lá) extends outward
        float stemLen = sock->stemRadius * 0.65f + 0.025f;
        Vector3 bladeBase = Vector3Add(sock->pos, Vector3Scale(stemDir, stemLen));

        // Leaf growth direction: tilted outward and forward along branch
        Vector3 leafDir  = Vector3Normalize(Vector3Add(Vector3Scale(stemDir, 0.55f), Vector3Scale(spineDir, 0.45f)));
        Vector3 leafNorm = Vector3Normalize(Vector3CrossProduct(stemDir, spineDir));
        Vector3 leafSide = Vector3Normalize(Vector3CrossProduct(leafDir, leafNorm));

        // High frequency biological wind flutter
        float flutterPhase = time * 7.5f + (float)k * 1.85f;
        float sway = (config->swayAmp > 0.001f ? config->swayAmp : 0.035f);
        float flutter = sinf(flutterPhase) * sway * (0.5f + 0.5f * sock->arc);
        Vector3 flutterVec = Vector3Scale(leafNorm, flutter);

        // Petiole tiny stem segment
        Vector3 vStemBase = sock->pos;
        Vector3 vRoot     = bladeBase;

        Color cV = leafVein;
        Color cB = leafBase;
        Color cM = leafMid;
        Color cT = leafTip;

        // Draw Petiole stem
        if (!isShadowPass)
        {
            float stemLit = 0.35f + 0.65f * fmaxf(Vector3DotProduct(stemDir, sunDir), 0.0f);
            rlColor4ub((unsigned char)(cV.r * stemLit * 0.8f), (unsigned char)(cV.g * stemLit * 0.8f), (unsigned char)(cV.b * stemLit * 0.8f), 255);
            rlNormal3f(stemDir.x, stemDir.y, stemDir.z);
            Vector3 sLeft  = Vector3Add(vStemBase, Vector3Scale(leafSide, -0.008f));
            Vector3 sRight = Vector3Add(vStemBase, Vector3Scale(leafSide,  0.008f));
            rlVertex3f(sLeft.x, sLeft.y, sLeft.z);
            rlVertex3f(sRight.x, sRight.y, sRight.z);
            rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
        }

        // ---------------------------------------------------------------------
        // 3D CUBIC BÉZIER BLADE (From map_props_nature.inl): OVAL, WILLOW, MAPLE
        // ---------------------------------------------------------------------
        if (config->shape == WOOD_LEAF_SHAPE_WILLOW)
        {
            // WILLOW TENDRIL: Elongated slender weeping S-curve Bézier arch
            BotanicalProfile prof = Botanical_ProfileWillow();
            float wLen = leafLen * 1.45f;
            float maxW = wLen * prof.W * 2.0f;

            Vector3 p0 = vRoot;
            Vector3 p1 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, wLen * 0.35f),
                                                      Vector3Scale(leafNorm, prof.curl * wLen * 0.15f)));
            Vector3 p2 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, wLen * 0.70f),
                                                      Vector3Add(Vector3Scale(leafNorm, prof.curl * wLen * 0.65f),
                                                                 Vector3Scale(flutterVec, 0.5f))));
            Vector3 p3 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, wLen),
                                                      Vector3Add(Vector3Scale(leafNorm, prof.curl * wLen),
                                                                 flutterVec)));

            Botanical_RenderBezierLeafBlade(p0, p1, p2, p3, leafNorm, maxW, prof.fold,
                                           cB, cM, cT, cV, sunDir, isShadowPass, 255);
        }
        else if (config->shape == WOOD_LEAF_SHAPE_MAPLE)
        {
            // MAPLE / SERRATED BRAMBLE: 3-Lobed Palmate Blade with Glowing Center Vein
            float mLen = leafLen * 1.15f;
            float mWidth = mLen * 0.54f;

            Vector3 vSpineMid = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.45f), Vector3Scale(leafNorm, mLen * 0.05f)));
            Vector3 vTip      = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 1.05f), flutterVec));

            Vector3 vLeftLobe  = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.60f),
                                                              Vector3Add(Vector3Scale(leafSide, -mWidth), Vector3Scale(leafNorm, mLen * 0.04f))));
            Vector3 vRightLobe = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.60f),
                                                              Vector3Add(Vector3Scale(leafSide,  mWidth), Vector3Scale(leafNorm, mLen * 0.04f))));

            Vector3 vNotchL = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.68f), Vector3Scale(leafSide, -mWidth * 0.35f)));
            Vector3 vNotchR = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.68f), Vector3Scale(leafSide,  mWidth * 0.35f)));
            Vector3 vBaseL  = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.25f), Vector3Scale(leafSide, -mWidth * 0.55f)));
            Vector3 vBaseR  = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, mLen * 0.25f), Vector3Scale(leafSide,  mWidth * 0.55f)));

            Vector3 nC = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vLeftLobe, vRoot), Vector3Subtract(vTip, vRoot)));
            Vector3 nL = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vBaseL, vRoot), Vector3Subtract(vLeftLobe, vRoot)));
            Vector3 nR = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(vRightLobe, vRoot), Vector3Subtract(vBaseR, vRoot)));

            if (!isShadowPass)
            {
                float dC = 0.38f + 0.62f * Botanical_WrapDiffuse(nC, sunDir) + 0.35f * Botanical_WrapDiffuse(Vector3Negate(nC), sunDir);
                float dL = 0.38f + 0.62f * Botanical_WrapDiffuse(nL, sunDir) + 0.35f * Botanical_WrapDiffuse(Vector3Negate(nL), sunDir);
                float dR = 0.38f + 0.62f * Botanical_WrapDiffuse(nR, sunDir) + 0.35f * Botanical_WrapDiffuse(Vector3Negate(nR), sunDir);

                // Central Lobe with glowing vein
                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z);
                rlColor4ub((unsigned char)(cB.r * dC), (unsigned char)(cB.g * dC), (unsigned char)(cB.b * dC), 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);
                rlColor4ub((unsigned char)(cT.r * dC), (unsigned char)(cT.g * dC), (unsigned char)(cT.b * dC), 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vTip.x, vTip.y, vTip.z);

                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z);
                rlColor4ub((unsigned char)(cT.r * dC), (unsigned char)(cT.g * dC), (unsigned char)(cT.b * dC), 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
                rlColor4ub((unsigned char)(cB.r * dC), (unsigned char)(cB.g * dC), (unsigned char)(cB.b * dC), 255);
                rlNormal3f(nC.x, nC.y, nC.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);

                // Left Lobe
                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
                rlColor4ub((unsigned char)(cB.r * dL), (unsigned char)(cB.g * dL), (unsigned char)(cB.b * dL), 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vBaseL.x, vBaseL.y, vBaseL.z);
                rlColor4ub((unsigned char)(cT.r * dL), (unsigned char)(cT.g * dL), (unsigned char)(cT.b * dL), 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);

                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
                rlColor4ub((unsigned char)(cT.r * dL), (unsigned char)(cT.g * dL), (unsigned char)(cT.b * dL), 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);
                rlColor4ub((unsigned char)(cB.r * dL), (unsigned char)(cB.g * dL), (unsigned char)(cB.b * dL), 255);
                rlNormal3f(nL.x, nL.y, nL.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);

                // Right Lobe
                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
                rlColor4ub((unsigned char)(cT.r * dR), (unsigned char)(cT.g * dR), (unsigned char)(cT.b * dR), 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
                rlColor4ub((unsigned char)(cB.r * dR), (unsigned char)(cB.g * dR), (unsigned char)(cB.b * dR), 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vBaseR.x, vBaseR.y, vBaseR.z);

                rlColor4ub(cV.r, cV.g, cV.b, 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRoot.x, vRoot.y, vRoot.z);
                rlColor4ub((unsigned char)(cB.r * dR), (unsigned char)(cB.g * dR), (unsigned char)(cB.b * dR), 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);
                rlColor4ub((unsigned char)(cT.r * dR), (unsigned char)(cT.g * dR), (unsigned char)(cT.b * dR), 255);
                rlNormal3f(nR.x, nR.y, nR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
            }
            else
            {
                rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z); rlVertex3f(vTip.x, vTip.y, vTip.z);
                rlVertex3f(vSpineMid.x, vSpineMid.y, vSpineMid.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z);
                rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vBaseL.x, vBaseL.y, vBaseL.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z);
                rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vLeftLobe.x, vLeftLobe.y, vLeftLobe.z); rlVertex3f(vNotchL.x, vNotchL.y, vNotchL.z);
                rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z); rlVertex3f(vBaseR.x, vBaseR.y, vBaseR.z);
                rlVertex3f(vRoot.x, vRoot.y, vRoot.z); rlVertex3f(vNotchR.x, vNotchR.y, vNotchR.z); rlVertex3f(vRightLobe.x, vRightLobe.y, vRightLobe.z);
            }
        }
        else
        {
            // OVAL BROADLEAF: Lush 3D Cubic Bézier Cantilever Blade with Glowing Chi Midrib
            BotanicalProfile prof = Botanical_ProfileOval();
            float maxW = leafLen * prof.W * 2.0f;

            Vector3 p0 = vRoot;
            Vector3 p1 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, leafLen * 0.35f),
                                                      Vector3Scale(leafNorm, prof.curl * leafLen * 0.12f)));
            Vector3 p2 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, leafLen * 0.70f),
                                                      Vector3Add(Vector3Scale(leafNorm, prof.curl * leafLen * 0.55f),
                                                                 Vector3Scale(flutterVec, 0.5f))));
            Vector3 p3 = Vector3Add(vRoot, Vector3Add(Vector3Scale(leafDir, leafLen),
                                                      Vector3Add(Vector3Scale(leafNorm, prof.curl * leafLen),
                                                                 flutterVec)));

            Botanical_RenderBezierLeafBlade(p0, p1, p2, p3, leafNorm, maxW, prof.fold,
                                           cB, cM, cT, cV, sunDir, isShadowPass, 255);
        }
    }

    rlEnd();
    if (!isShadowPass) EndBlendMode();
    rlEnableBackfaceCulling();
}

#endif // VC_WOOD_LEAVES_INL
