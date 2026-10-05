#ifndef VC_WOOD_FLOWER_INL
#define VC_WOOD_FLOWER_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/composition/visual_composer.h"
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

    // ── Mode B: Standalone / Free Petals Shower (Wind, Gravity & Air Drag) ────
    if (!config->attached || config->sockets == NULL || config->socketCount <= 0)
    {
        static float s_freeFlowerTimer = 0.0f;
        s_freeFlowerTimer += GetFrameTime();
        int targetCount = config->count > 0 ? config->count : 48;

        if (s_freeFlowerTimer > 1.4f || VFX_FoliageSystem_GetActiveCount() < targetCount / 3)
        {
            s_freeFlowerTimer = 0.0f;
            VFX_FoliageSpawnParams sp = VFX_FoliageSpawnParams_Default();
            sp.kind = BOTANICAL_KIND_PETAL;
            sp.flowerType = config->type;
            sp.style = config->style;
            sp.origin = config->origin;
            sp.radius = config->radius > 0.1f ? config->radius : 1.2f;
            sp.count = targetCount;
            sp.attached = false;
            sp.mass = config->mass > 1e-4f ? config->mass : 0.002f;
            sp.initialVelocity = config->initialVelocity;
            sp.velocitySpread = config->velocitySpread > 0.1f ? config->velocitySpread : 1.0f;
            sp.size = config->size > 0.02f ? config->size : 0.13f;
            sp.growth = config->growth;
            sp.lifetime = 12.0f;
            sp.seed = config->seed;
            VFX_FoliageSystem_SpawnCluster(&sp);
        }
        return;
    }

    // ── Mode A: Attached Blooming Flowers on Vine / Tree Host ─────────────────
    bool isShadowPass = EnvShadow_IsCapturing();
    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float time = (float)GetTime();

    // Palette setup based on style and wither
    Color petalBase, petalTip, stamenGlow;
    switch (config->style)
    {
        case WOOD_VINE_STYLE_BLOOD_BRAMBLE: // Scarlet Bramble Orchid: Deep crimson to fiery rose
            petalBase  = (Color){160, 18, 30, 255};
            petalTip   = (Color){245, 60, 85, 255};
            stamenGlow = (Color){255, 210, 80, 255}; // Golden ember core
            break;
        case WOOD_VINE_STYLE_GOLDEN_AMBER:  // Celestial Osmanthus / Golden Lotus: Honey-amber
            petalBase  = (Color){210, 140, 20, 255};
            petalTip   = (Color){255, 220, 90, 255};
            stamenGlow = (Color){255, 250, 180, 255}; // Radiant white-gold core
            break;
        case WOOD_VINE_STYLE_WITHER_GHOST:  // Ghost Orchid: Spectral pale violet to ethereal cyan
            petalBase  = (Color){130, 95, 160, 255};
            petalTip   = (Color){195, 160, 245, 255};
            stamenGlow = (Color){120, 230, 255, 255}; // Glowing phantom cyan
            break;
        case WOOD_VINE_STYLE_JADE_EMERALD:  // Jade Blossom: Pure pale jade transitioning to translucent white tip
        default:
            petalBase  = (Color){35, 175, 105, 255};
            petalTip   = (Color){180, 255, 225, 255};
            stamenGlow = (Color){255, 235, 90, 255}; // Radiant golden pistil
            break;
    }

    // Wither browning decay
    if (config->wither > 0.05f)
    {
        float w = config->wither;
        Color wiltBrown = (Color){95, 60, 35, 255};
        petalBase.r = (unsigned char)(petalBase.r * (1.0f - w) + wiltBrown.r * w);
        petalBase.g = (unsigned char)(petalBase.g * (1.0f - w) + wiltBrown.g * w);
        petalBase.b = (unsigned char)(petalBase.b * (1.0f - w) + wiltBrown.b * w);

        petalTip.r = (unsigned char)(petalTip.r * (1.0f - w) + wiltBrown.r * w);
        petalTip.g = (unsigned char)(petalTip.g * (1.0f - w) + wiltBrown.g * w);
        petalTip.b = (unsigned char)(petalTip.b * (1.0f - w) + wiltBrown.b * w);
    }

    rlDisableBackfaceCulling();
    if (!isShadowPass) BeginBlendMode(BLEND_ALPHA);

    rlBegin(RL_TRIANGLES);

    for (int s = 0; s < config->socketCount; s++)
    {
        const VFX_BotanicalSocket *sock = &config->sockets[s];

        // Staggered biological emergence along vine
        float nodeBirth = sock->arc * 0.35f;
        if (config->growth < nodeBirth) continue;
        float localGrowth = (config->growth - nodeBirth) / (1.0f - nodeBirth);
        if (localGrowth > 1.0f) localGrowth = 1.0f;

        // Unfurling bloom curve: bud opens from 0 to full chalice
        float bloomFactor = WoodFlower_Smoothstep(0.0f, 0.85f, localGrowth);
        float blossomScale = config->size * WoodFlower_Smoothstep(0.0f, 0.40f, localGrowth);

        // Wind sway flutter
        float flutter = sinf(time * 3.4f + (float)s * 1.57f) * config->swayAmp;
        Vector3 stemNormal = Vector3Normalize(Vector3Add(sock->normal, (Vector3){flutter, flutter * 0.5f, -flutter}));
        Vector3 stemTangent = sock->tangent;
        Vector3 stemBinormal = Vector3Normalize(Vector3CrossProduct(stemNormal, stemTangent));

        // Center position of flower head (pedicel stem raises flower 0.05m off bark)
        Vector3 flowerCenter = Vector3Add(sock->pos, Vector3Scale(stemNormal, 0.045f));

        // Blossom morphology configuration
        int petalCount = 6;
        int layers = 1;
        float petalAspect = 0.55f; // width/length ratio

        switch (config->type)
        {
            case WOOD_FLOWER_TYPE_PLUM_BLOSSOM:
                petalCount = 5;
                layers = 1;
                petalAspect = 0.65f;
                break;
            case WOOD_FLOWER_TYPE_LOTUS:
                petalCount = 8;
                layers = 2; // Inner and outer petal tier
                petalAspect = 0.48f;
                break;
            case WOOD_FLOWER_TYPE_ORCHID:
                petalCount = 5;
                layers = 1;
                petalAspect = 0.50f;
                break;
            default:
                break;
        }

        // 1. DRAW PETALS LAYER BY LAYER
        for (int layer = 0; layer < layers; layer++)
        {
            float layerScale = (layer == 0) ? blossomScale : blossomScale * 0.72f;
            float layerOpenAngle = (layer == 0) 
                ? (bloomFactor * 68.0f * DEG2RAD)   // Outer petals spread wide
                : (bloomFactor * 42.0f * DEG2RAD);  // Inner petals cup inward
            float layerAngleOffset = (layer == 1) ? (PI / (float)petalCount) : 0.0f;

            for (int p = 0; p < petalCount; p++)
            {
                float angle = ((float)p / (float)petalCount) * 2.0f * PI + layerAngleOffset;
                Vector3 radialDir = Vector3Normalize(Vector3Add(
                    Vector3Scale(stemBinormal, cosf(angle)),
                    Vector3Scale(stemTangent, sinf(angle))
                ));

                // Petal direction curves outward as flower opens
                Vector3 petalDir = Vector3Normalize(Vector3Add(
                    Vector3Scale(stemNormal, cosf(layerOpenAngle)),
                    Vector3Scale(radialDir, sinf(layerOpenAngle))
                ));
                Vector3 petalSide = Vector3Normalize(Vector3CrossProduct(petalDir, stemNormal));

                float petalLen = layerScale;
                float petalWidth = layerScale * petalAspect;

                // 3 Petal vertices: Base -> Mid-Flank Left & Right -> Petal Tip
                Vector3 vBase = flowerCenter;
                Vector3 vMid = Vector3Add(flowerCenter, Vector3Scale(petalDir, petalLen * 0.55f));
                Vector3 vLeft = Vector3Add(vMid, Vector3Scale(petalSide, -petalWidth * 0.5f));
                Vector3 vRight = Vector3Add(vMid, Vector3Scale(petalSide, petalWidth * 0.5f));
                Vector3 vTip = Vector3Add(flowerCenter, Vector3Scale(petalDir, petalLen));

                // Subsurface lighting & Shading
                Vector3 petalNormal = Vector3Normalize(Vector3CrossProduct(petalSide, petalDir));
                float NdotL = fabsf(Vector3DotProduct(petalNormal, sunDir));
                float wrapLit = NdotL * 0.45f + 0.55f; // Thin tissue light transmission

                Color cBase = isShadowPass ? WHITE : (Color){
                    (unsigned char)(petalBase.r * wrapLit),
                    (unsigned char)(petalBase.g * wrapLit),
                    (unsigned char)(petalBase.b * wrapLit),
                    petalBase.a
                };

                Color cTip = isShadowPass ? WHITE : (Color){
                    (unsigned char)(petalTip.r * wrapLit),
                    (unsigned char)(petalTip.g * wrapLit),
                    (unsigned char)(petalTip.b * wrapLit),
                    petalTip.a
                };

                // Lower triangle: Base -> Left -> Right
                rlColor4ub(cBase.r, cBase.g, cBase.b, cBase.a);
                rlVertex3f(vBase.x, vBase.y, vBase.z);
                rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
                rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
                rlVertex3f(vRight.x, vRight.y, vRight.z);

                // Upper triangle: Left -> Tip -> Right
                rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
                rlVertex3f(vLeft.x, vLeft.y, vLeft.z);
                rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
                rlVertex3f(vTip.x, vTip.y, vTip.z);
                rlColor4ub(cTip.r, cTip.g, cTip.b, cTip.a);
                rlVertex3f(vRight.x, vRight.y, vRight.z);
            }
        }

        // 2. BIOLUMINESCENT CENTRAL PISTIL / STAMEN ORB (when not in shadow pass)
        if (!isShadowPass && bloomFactor > 0.15f)
        {
            float stamenR = blossomScale * 0.18f * bloomFactor;
            Vector3 stamenCenter = Vector3Add(flowerCenter, Vector3Scale(stemNormal, stamenR * 0.8f));

            // Central golden/celestial stamen dome (octahedral fan)
            for (int i = 0; i < 6; i++)
            {
                float a1 = ((float)i / 6.0f) * 2.0f * PI;
                float a2 = ((float)(i + 1) / 6.0f) * 2.0f * PI;

                Vector3 p1 = Vector3Add(stamenCenter, Vector3Add(
                    Vector3Scale(stemBinormal, cosf(a1) * stamenR),
                    Vector3Scale(stemTangent, sinf(a1) * stamenR)));
                Vector3 p2 = Vector3Add(stamenCenter, Vector3Add(
                    Vector3Scale(stemBinormal, cosf(a2) * stamenR),
                    Vector3Scale(stemTangent, sinf(a2) * stamenR)));
                Vector3 pTip = Vector3Add(stamenCenter, Vector3Scale(stemNormal, stamenR * 1.2f));

                rlColor4ub(stamenGlow.r, stamenGlow.g, stamenGlow.b, 255);
                rlVertex3f(pTip.x, pTip.y, pTip.z);
                rlVertex3f(p1.x, p1.y, p1.z);
                rlVertex3f(p2.x, p2.y, p2.z);
            }
        }
    }

    rlEnd();

    if (!isShadowPass) EndBlendMode();
    rlEnableBackfaceCulling();
}

#endif // VC_WOOD_FLOWER_INL
