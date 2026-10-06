#ifndef VC_WOOD_FLOWER_INL
#define VC_WOOD_FLOWER_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
#include "core/composition/wood/vc_wood_botanical_math.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

VFX_WoodFlowerConfig VFX_WoodFlower_DefaultConfig(void)
{
    VFX_WoodFlowerConfig cfg;
    cfg.attached = false;
    cfg.origin = (Vector3){0.0f, 3.0f, 0.0f};
    cfg.radius = 1.2f;
    cfg.count = 48;
    cfg.mass = 0.002f;
    cfg.initialVelocity = (Vector3){0.0f, 0.4f, 0.0f};
    cfg.velocitySpread = 1.0f;
    cfg.sockets = NULL;
    cfg.socketCount = 0;
    cfg.growth = 1.0f;
    cfg.wither = 0.0f;
    cfg.swayAmp = 0.030f;
    cfg.size = 0.14f;
    cfg.type = WOOD_FLOWER_TYPE_LOTUS;
    cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
    cfg.seed = 54321;
    return cfg;
}

const char* VFX_WoodFlowerType_Name(VFX_WoodFlowerType type)
{
    switch (type)
    {
        case WOOD_FLOWER_TYPE_LOTUS:        return "SACRED LOTUS";
        case WOOD_FLOWER_TYPE_ORCHID:       return "CELESTIAL ORCHID";
        case WOOD_FLOWER_TYPE_PLUM_BLOSSOM: return "IRONWOOD PLUM BLOSSOM";
        default:                            return "UNKNOWN FLOWER";
    }
}

static const char *s_woodFlowerTypeDisplayNames[WOOD_FLOWER_TYPE_COUNT] = {
    "SACRED LOTUS", "CELESTIAL ORCHID", "IRONWOOD PLUM BLOSSOM"
};

int VFX_WoodFlower_GetParams(VFX_WoodFlowerConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Type", .group = "Flower", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->type, .minInt = 0, .maxInt = WOOD_FLOWER_TYPE_COUNT - 1,
            .enumNames = s_woodFlowerTypeDisplayNames, .enumCount = WOOD_FLOWER_TYPE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Style", .group = "Flower", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->style, .minInt = 0, .maxInt = WOOD_VINE_STYLE_COUNT - 1,
            .enumNames = s_woodLeafStyleDisplayNames, .enumCount = WOOD_VINE_STYLE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Attached Mode", .group = "Flower", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->attached
        };
    }
    return n;
}

static inline float WoodFlower_Smoothstep(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

/* Renders procedural 3D blooming flowers on botanical sockets (ATTACHED)
 * or emits physical flower petals drifting in wind & gravity (FREE). */
void VFX_ComposeWoodFlower(const VFX_WoodFlowerConfig *config)
{
    if (config == NULL || config->growth <= 0.01f)
        return;

    VFX_BotanicalSocket fallbackSocket;
    const VFX_BotanicalSocket *socketsToRender = config->sockets;
    int socketCountToRender = config->socketCount;

    // ── Mode B: Standalone / Free Petals Shower (Wind, Gravity & Air Drag) ────
    if (!config->attached || config->sockets == NULL || config->socketCount <= 0)
    {
        // Render 1 standalone showcase flower at config->origin so the blossom is always visible
        fallbackSocket.pos = config->origin;
        fallbackSocket.normal = (Vector3){0.0f, 0.40f, 0.91f}; // Tilt toward camera
        fallbackSocket.tangent = (Vector3){0.0f, 1.0f, 0.0f};
        fallbackSocket.arc = 0.5f;
        fallbackSocket.stemRadius = 0.04f;
        socketsToRender = &fallbackSocket;
        socketCountToRender = 1;

        static float s_freeFlowerTimer = 0.0f;
        s_freeFlowerTimer += GetFrameTime();
        int targetCount = config->count > 0 ? config->count : 48;

        if (s_freeFlowerTimer > 1.4f || VFX_FoliageSystem_GetActiveCount() < targetCount / 3)
        {
            s_freeFlowerTimer = 0.0f;
            VFX_FoliageSpawnParams sp = VFX_FoliageSpawnParams_Default();
            sp.kind = BOTANICAL_KIND_FLOWER_HEAD; // Intact whole blossoms tumble and fall!
            sp.flowerType = config->type;
            sp.style = config->style;
            sp.origin = config->origin;
            sp.radius = config->radius > 0.1f ? config->radius : 1.2f;
            sp.count = targetCount;
            sp.attached = false;
            sp.mass = config->mass > 0 ? config->mass : 0.012f; // Compound head proxy.
            sp.initialVelocity = config->initialVelocity;
            sp.velocitySpread = config->velocitySpread > 0.1f ? config->velocitySpread : 1.0f;
            sp.size = config->size > 0.02f ? config->size : 0.14f;
            sp.growth = config->growth;
            sp.lifetime = 12.0f;
            sp.seed = config->seed;
            VFX_FoliageSystem_SpawnCluster(&sp);
        }
    }

    // ── Mode A: Attached / Showcase Blooming Flowers ──────────────────────────
    bool isShadowPass = EnvShadow_IsCapturing();
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float time = (float)GetTime();

    // Vibrant celestial floral palette based on type and style — MAGICAL SPIRIT EMISSION
    Color petalBase, petalMid, petalTip, cGlowRim, stamenGlow, apexColor, podColor;
    if (config->type == WOOD_FLOWER_TYPE_LOTUS)
    {
        // SACRED LOTUS (Hoa Sen): Iridescent White-Pink base to vivid Lotus Rose + celestial glowing heart
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase   = (Color){185, 25, 42, 255};
            petalMid    = (Color){255, 45, 75, 255};
            petalTip    = (Color){255, 110, 140, 255};
            cGlowRim    = (Color){255, 160, 180, 255};
            stamenGlow  = (Color){255, 220, 80, 255};
            apexColor   = (Color){255, 255, 200, 255};
            podColor    = (Color){235, 175, 45, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            petalBase   = (Color){255, 245, 190, 255};
            petalMid    = (Color){255, 195, 35, 255};
            petalTip    = (Color){255, 225, 90, 255};
            cGlowRim    = (Color){255, 245, 170, 255};
            stamenGlow  = (Color){255, 248, 140, 255};
            apexColor   = (Color){255, 255, 230, 255};
            podColor    = (Color){250, 210, 60, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase   = (Color){215, 200, 245, 255};
            petalMid    = (Color){185, 135, 255, 255};
            petalTip    = (Color){220, 185, 255, 255};
            cGlowRim    = (Color){240, 215, 255, 255};
            stamenGlow  = (Color){150, 240, 255, 255};
            apexColor   = (Color){230, 250, 255, 255};
            podColor    = (Color){140, 200, 245, 255};
        } else {
            // Default Celestial Jade: Pure spiritual iridescent blush to radiant lotus rose magenta
            petalBase   = (Color){255, 245, 250, 255};
            petalMid    = (Color){255, 85, 165, 255};
            petalTip    = (Color){255, 155, 215, 255};
            cGlowRim    = (Color){255, 195, 235, 255};
            stamenGlow  = (Color){255, 225, 55, 255};
            apexColor   = (Color){255, 255, 220, 255};
            podColor    = (Color){245, 205, 45, 255};
        }
    }
    else if (config->type == WOOD_FLOWER_TYPE_ORCHID)
    {
        // CELESTIAL ORCHID (Hoa Lan): Silky Porcelain to Royal Orchid Violet
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase   = (Color){245, 210, 220, 255};
            petalMid    = (Color){225, 35, 75, 255};
            petalTip    = (Color){255, 110, 145, 255};
            cGlowRim    = (Color){255, 180, 205, 255};
            stamenGlow  = (Color){255, 220, 90, 255};
            apexColor   = (Color){255, 255, 210, 255};
            podColor    = (Color){225, 180, 50, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            petalBase   = (Color){255, 248, 220, 255};
            petalMid    = (Color){240, 175, 25, 255};
            petalTip    = (Color){255, 215, 70, 255};
            cGlowRim    = (Color){255, 245, 160, 255};
            stamenGlow  = (Color){255, 230, 120, 255};
            apexColor   = (Color){255, 255, 220, 255};
            podColor    = (Color){245, 200, 50, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase   = (Color){230, 220, 250, 255};
            petalMid    = (Color){175, 130, 245, 255};
            petalTip    = (Color){215, 180, 255, 255};
            cGlowRim    = (Color){240, 210, 255, 255};
            stamenGlow  = (Color){110, 225, 255, 255};
            apexColor   = (Color){210, 245, 255, 255};
            podColor    = (Color){130, 195, 245, 255};
        } else {
            petalBase   = (Color){252, 248, 255, 255};
            petalMid    = (Color){215, 95, 245, 255};
            petalTip    = (Color){245, 175, 255, 255};
            cGlowRim    = (Color){255, 215, 255, 255};
            stamenGlow  = (Color){255, 225, 75, 255};
            apexColor   = (Color){255, 255, 235, 255};
            podColor    = (Color){245, 205, 55, 255};
        }
    }
    else // WOOD_FLOWER_TYPE_PLUM
    {
        // PLUM BLOSSOM (Hoa Mai / Hoa Đào): Radiant Peach Bloom or Golden Apricot
        if (config->style == WOOD_VINE_STYLE_BLOOD_BRAMBLE) {
            petalBase   = (Color){255, 225, 230, 255};
            petalMid    = (Color){245, 55, 85, 255};
            petalTip    = (Color){255, 125, 155, 255};
            cGlowRim    = (Color){255, 175, 195, 255};
            stamenGlow  = (Color){255, 215, 65, 255};
            apexColor   = (Color){255, 255, 210, 255};
            podColor    = (Color){225, 170, 40, 255};
        } else if (config->style == WOOD_VINE_STYLE_GOLDEN_AMBER) {
            // Mai Vàng
            petalBase   = (Color){255, 250, 195, 255};
            petalMid    = (Color){255, 205, 30, 255};
            petalTip    = (Color){255, 230, 80, 255};
            cGlowRim    = (Color){255, 248, 160, 255};
            stamenGlow  = (Color){255, 150, 20, 255};
            apexColor   = (Color){255, 255, 220, 255};
            podColor    = (Color){245, 195, 45, 255};
        } else if (config->style == WOOD_VINE_STYLE_WITHER_GHOST) {
            petalBase   = (Color){235, 225, 245, 255};
            petalMid    = (Color){195, 165, 250, 255};
            petalTip    = (Color){225, 200, 255, 255};
            cGlowRim    = (Color){245, 225, 255, 255};
            stamenGlow  = (Color){140, 240, 255, 255};
            apexColor   = (Color){220, 250, 255, 255};
            podColor    = (Color){145, 205, 250, 255};
        } else {
            // Hoa Đào
            petalBase   = (Color){255, 245, 248, 255};
            petalMid    = (Color){255, 105, 175, 255};
            petalTip    = (Color){255, 165, 215, 255};
            cGlowRim    = (Color){255, 205, 235, 255};
            stamenGlow  = (Color){255, 225, 60, 255};
            apexColor   = (Color){255, 255, 225, 255};
            podColor    = (Color){245, 200, 40, 255};
        }
    }

    // Wither browning decay
    if (config->wither > 0.05f)
    {
        float w = config->wither;
        Color wiltBrown = (Color){95, 60, 35, 255};
        petalBase.r = (unsigned char)(petalBase.r * (1.0f - w) + wiltBrown.r * w);
        petalBase.g = (unsigned char)(petalBase.g * (1.0f - w) + wiltBrown.g * w);
        petalBase.b = (unsigned char)(petalBase.b * (1.0f - w) + wiltBrown.b * w);

        petalMid.r = (unsigned char)(petalMid.r * (1.0f - w) + wiltBrown.r * w);
        petalMid.g = (unsigned char)(petalMid.g * (1.0f - w) + wiltBrown.g * w);
        petalMid.b = (unsigned char)(petalMid.b * (1.0f - w) + wiltBrown.b * w);

        petalTip.r = (unsigned char)(petalTip.r * (1.0f - w) + wiltBrown.r * w);
        petalTip.g = (unsigned char)(petalTip.g * (1.0f - w) + wiltBrown.g * w);
        petalTip.b = (unsigned char)(petalTip.b * (1.0f - w) + wiltBrown.b * w);
    }

    rlDisableBackfaceCulling();
    if (!isShadowPass) BeginBlendMode(BLEND_ALPHA);

    rlBegin(RL_TRIANGLES);

    for (int s = 0; s < socketCountToRender; s++)
    {
        const VFX_BotanicalSocket *sock = &socketsToRender[s];

        // Staggered biological emergence along vine
        float nodeBirth = sock->arc * 0.35f;
        if (config->growth < nodeBirth) continue;
        float localGrowth = (config->growth - nodeBirth) / (1.0f - nodeBirth);
        if (localGrowth > 1.0f) localGrowth = 1.0f;

        // 4-STAGE ORGANIC BUD-TO-BLOOM UNFORLING CURVE
        // When localGrowth is small (0.0 -> 0.25): Closed conical/egg bud (nụ hoa)
        // When localGrowth progresses (0.25 -> 0.75): Calyx reflexes, petals peel open, bloom flourishes
        // When localGrowth is mature (0.75 -> 1.0): Full celestial blossom with incandescent stamen core
        float budScale = 0.42f + 0.58f * WoodFlower_Smoothstep(0.0f, 0.70f, localGrowth);
        float blossomScale = config->size * budScale;
        float bloomFactor = WoodFlower_Smoothstep(0.25f, 0.85f, localGrowth);

        // Wind sway flutter with phase offset per flower
        float flutter = sinf(time * 3.4f + (float)s * 1.57f) * config->swayAmp;
        Vector3 stemNormal   = Vector3Normalize(Vector3Add(sock->normal, (Vector3){flutter, flutter * 0.5f, -flutter}));
        Vector3 stemTangent  = sock->tangent;
        Vector3 stemBinormal = Vector3Normalize(Vector3CrossProduct(stemNormal, stemTangent));

        // Pedicel raises flower head 0.045m off host surface
        Vector3 flowerCenter = Vector3Add(sock->pos, Vector3Scale(stemNormal, 0.045f));

        // 3D Morphological Calyx Sepals (Lá Đài Bảo Vệ Nụ & Khung Nở)
        // In bud: spread is 0.06 rad (upright closed chalice hugging petals).
        // In bloom: spread is 1.28 rad (reflexed backward against stem).
        float calyxProg = WoodFlower_Smoothstep(0.12f, 0.68f, localGrowth);
        float calyxSpread = 0.06f + calyxProg * 1.22f;
        float calyxLen = blossomScale * 0.46f;
        float calyxW   = calyxLen * 0.40f;
        Color sepalBase = (Color){24, 90, 42, 255};
        Color sepalTip  = (Color){60, 185, 95, 255};
        Botanical_RenderCalyxSepals(
            flowerCenter, stemNormal, stemBinormal, stemTangent,
            calyxLen, calyxW, calyxSpread,
            sepalBase, sepalTip,
            sunDir, isShadowPass, 255
        );

        // ---------------------------------------------------------------------
        // 1. SACRED LOTUS (HOA SEN): 3D Dished Parametric Petals + Phyllotaxis Dome
        // ---------------------------------------------------------------------
        if (config->type == WOOD_FLOWER_TYPE_LOTUS)
        {
            const int outerCount = 5;
            const int innerCount = 4;
            BotanicalProfile prof = Botanical_ProfilePetalLotus();

            // Outer Petal Tier: Starts tightly clasping bud cone (curve < 0), then peels open
            float bOuter = Botanical_EaseOutBack(fminf(fmaxf((localGrowth - 0.12f) / 0.68f, 0.0f), 1.0f));
            float outerSpread = 0.05f + bOuter * 1.25f;
            float outerLen = blossomScale * 1.08f;
            float outerCurve = -0.16f * outerLen * (1.0f - bOuter) + 0.36f * outerLen * bOuter;

            for (int p = 0; p < outerCount; p++)
            {
                float ang = ((float)p / (float)outerCount) * 2.0f * PI;
                Botanical_RenderParametricPetal(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    ang, outerSpread, outerLen, &prof,
                    outerCurve, 0.05f,
                    petalBase, petalMid, petalTip, cGlowRim,
                    sunDir, isShadowPass, 255
                );
            }

            // Inner Petal Tier: Opens with staggered delay behind outer petals
            float bInner = Botanical_EaseOutBack(fminf(fmaxf((localGrowth - 0.28f) / 0.72f, 0.0f), 1.0f));
            float innerSpread = 0.03f + bInner * 0.68f;
            float innerLen = blossomScale * 0.82f;
            float innerCurve = -0.18f * innerLen * (1.0f - bInner) + 0.44f * innerLen * bInner;
            float offsetAng = PI / (float)innerCount;

            for (int p = 0; p < innerCount; p++)
            {
                float ang = ((float)p / (float)innerCount) * 2.0f * PI + offsetAng;
                Botanical_RenderParametricPetal(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    ang, innerSpread, innerLen, &prof,
                    innerCurve, -0.06f,
                    petalBase, petalMid, petalTip, cGlowRim,
                    sunDir, isShadowPass, 255
                );
            }

            // 3D Central Receptacle Dome: completely concealed inside tight bud, emerges in bloom
            if (bloomFactor > 0.15f)
            {
                float podR = blossomScale * 0.24f * bloomFactor;
                Botanical_RenderFlowerCenterDome(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    podR, podR * 0.55f, podColor, apexColor, stamenGlow,
                    time, s, isShadowPass, 255
                );
            }
        }

        // ---------------------------------------------------------------------
        // 2. CELESTIAL ORCHID (HOA LAN): Bilateral Symmetry + Sculpted Labellum Lip
        // ---------------------------------------------------------------------
        else if (config->type == WOOD_FLOWER_TYPE_ORCHID)
        {
            float oScale = blossomScale * 1.15f;
            BotanicalProfile prof = Botanical_ProfilePetalOrchid();

            float angles[5]  = { 0.0f, 85.0f * DEG2RAD, -85.0f * DEG2RAD, 140.0f * DEG2RAD, -140.0f * DEG2RAD };
            float lengths[5] = { oScale * 1.10f, oScale * 0.95f, oScale * 0.95f, oScale * 0.85f, oScale * 0.85f };

            for (int p = 0; p < 5; p++)
            {
                float kRank = (float)p / 4.0f;
                float delay = 0.12f + (1.0f - kRank) * 0.22f;
                float bRaw = fminf(fmaxf((localGrowth - delay) / 0.66f, 0.0f), 1.0f);
                float b = Botanical_EaseOutBack(bRaw);
                float oSpread = 0.05f + b * 1.02f;
                float oCurve = -0.14f * lengths[p] * (1.0f - b) + 0.32f * lengths[p] * b;

                Botanical_RenderParametricPetal(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    angles[p], oSpread, lengths[p], &prof,
                    oCurve, (p % 2 == 0) ? 0.06f : -0.06f,
                    petalBase, petalMid, petalTip, cGlowRim,
                    sunDir, isShadowPass, 255
                );
            }

            // Central Orchid Labellum Lip (Cánh Môi Thần Lan) — unfolds from bud
            if (bloomFactor > 0.15f)
            {
                float bLip = Botanical_EaseOutBack(WoodFlower_Smoothstep(0.15f, 0.90f, localGrowth));
                float lipLen = oScale * 1.25f * bLip;
                float lipW   = oScale * 0.68f * bLip;
                Vector3 lipDir = Vector3Normalize(Vector3Add(Vector3Scale(stemNormal, 0.40f), Vector3Scale(stemTangent, 0.88f * bLip)));
                Vector3 lipSide = Vector3Normalize(Vector3CrossProduct(lipDir, stemNormal));
                Vector3 lipNorm = Vector3Normalize(Vector3CrossProduct(lipSide, lipDir));

                Vector3 lBase  = flowerCenter;
                Vector3 lMid   = Vector3Add(flowerCenter, Vector3Add(Vector3Scale(lipDir, lipLen * 0.50f), Vector3Scale(lipNorm, lipLen * 0.18f)));
                Vector3 lLeft  = Vector3Add(lMid, Vector3Scale(lipSide, -lipW * 0.5f));
                Vector3 lRight = Vector3Add(lMid, Vector3Scale(lipSide,  lipW * 0.5f));
                Vector3 lTip   = Vector3Add(flowerCenter, Vector3Add(Vector3Scale(lipDir, lipLen), Vector3Scale(lipNorm, -lipLen * 0.12f)));

                Color lipCol = stamenGlow;
                if (!isShadowPass)
                {
                    rlColor4ub(petalBase.r, petalBase.g, petalBase.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lBase.x, lBase.y, lBase.z);
                    rlColor4ub(lipCol.r, lipCol.g, lipCol.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lLeft.x, lLeft.y, lLeft.z);
                    rlColor4ub(lipCol.r, lipCol.g, lipCol.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lRight.x, lRight.y, lRight.z);

                    rlColor4ub(lipCol.r, lipCol.g, lipCol.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lLeft.x, lLeft.y, lLeft.z);
                    rlColor4ub(cGlowRim.r, cGlowRim.g, cGlowRim.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lTip.x, lTip.y, lTip.z);
                    rlColor4ub(lipCol.r, lipCol.g, lipCol.b, 255);
                    rlNormal3f(lipNorm.x, lipNorm.y, lipNorm.z); rlVertex3f(lRight.x, lRight.y, lRight.z);
                }
                else
                {
                    rlVertex3f(lBase.x, lBase.y, lBase.z); rlVertex3f(lLeft.x, lLeft.y, lLeft.z); rlVertex3f(lRight.x, lRight.y, lRight.z);
                    rlVertex3f(lLeft.x, lLeft.y, lLeft.z); rlVertex3f(lTip.x, lTip.y, lTip.z); rlVertex3f(lRight.x, lRight.y, lRight.z);
                }

                // Orchid stamen column
                Botanical_RenderFlowerCenterDome(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    oScale * 0.18f * bLip, oScale * 0.12f * bLip, podColor, apexColor, stamenGlow,
                    time, s, isShadowPass, 255
                );
            }
        }

        // ---------------------------------------------------------------------
        // 3. IRONWOOD PLUM BLOSSOM (HOA MAI / HOA ĐÀO): 5 Rounded Petals + Stamen Burst
        // ---------------------------------------------------------------------
        else
        {
            const int pCount = 5;
            BotanicalProfile prof = Botanical_ProfilePetalPlum();
            float petalLen = blossomScale * 0.98f;

            for (int p = 0; p < pCount; p++)
            {
                float kRank = (float)p / 4.0f;
                float delay = 0.12f + (1.0f - kRank) * 0.18f;
                float bRaw = fminf(fmaxf((localGrowth - delay) / 0.70f, 0.0f), 1.0f);
                float b = Botanical_EaseOutBack(bRaw);
                float spreadAngle = 0.05f + b * 1.18f;
                float pCurve = -0.15f * petalLen * (1.0f - b) + 0.28f * petalLen * b;
                float ang = ((float)p / (float)pCount) * 2.0f * PI;

                Botanical_RenderParametricPetal(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    ang, spreadAngle, petalLen, &prof,
                    pCurve, 0.04f,
                    petalBase, petalMid, petalTip, cGlowRim,
                    sunDir, isShadowPass, 255
                );
            }

            // Incandescent Golden Stamen Burst Dome
            if (bloomFactor > 0.18f)
            {
                float stamenR = petalLen * 0.32f * bloomFactor;
                Botanical_RenderFlowerCenterDome(
                    flowerCenter, stemNormal, stemBinormal, stemTangent,
                    stamenR, stamenR * 0.60f, podColor, apexColor, stamenGlow,
                    time, s, isShadowPass, 255
                );
            }
        }
    }

    rlEnd();

    if (!isShadowPass) EndBlendMode();
    rlEnableBackfaceCulling();
}

#endif // VC_WOOD_FLOWER_INL
