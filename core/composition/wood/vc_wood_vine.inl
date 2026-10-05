// ============================================================================
// VC_WOOD_VINE.INL — Primary Wood Growth Vine & Tendril
//
// Capabilities:
//   1. Model Wrapping Mode (when targetRadius > 0): Hugs & constricts target model.
//   2. Free Serpentine Mode: Organic snake-like reaching tendril with curl noise.
//   3. Wang 2008 Double Reflection RMF: Eliminates twist and singularity.
//   4. Phyllotaxis Hooked Thorns (Golden Angle 137.508°) with easeOutBack pop.
//   5. Braided Twin Tendrils: Multi-strand organic volume.
//   6. Ground Smart Shadow Projection: Connects 3D geometry to arena floor.
//   7. Wood shader: Bark fibrous ridges, wrap diffuse, and luminous sap pulses.
// ============================================================================

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "core/map_manager.h"

static Shader s_woodVineShader = {0};
static int s_locGrowth = -1;
static int s_locWither = -1;
static int s_locSapPulseFreq = -1;
static int s_locSapPulseSpeed = -1;
static int s_locBaseColor = -1;
static int s_locFoliageColor = -1;
static int s_locSapColor = -1;
static int s_locSwayAmp = -1;
static int s_locTime = -1;
static int s_locLightDir = -1;
static bool s_woodVineShaderReady = false;

static inline float WoodVine_Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

static void WoodVine_InitShader(void)
{
    if (s_woodVineShaderReady) return;

    s_woodVineShader = ResourceManager_LoadShader("core/shaders/wood_vine.vs", "core/shaders/wood_vine.fs");
    if (s_woodVineShader.id > 0)
    {
        s_locGrowth = GetShaderLocation(s_woodVineShader, "u_growth");
        s_locWither = GetShaderLocation(s_woodVineShader, "u_wither");
        s_locSapPulseFreq = GetShaderLocation(s_woodVineShader, "u_sapPulseFreq");
        s_locSapPulseSpeed = GetShaderLocation(s_woodVineShader, "u_sapPulseSpeed");
        s_locBaseColor = GetShaderLocation(s_woodVineShader, "u_baseColor");
        s_locFoliageColor = GetShaderLocation(s_woodVineShader, "u_foliageColor");
        s_locSapColor = GetShaderLocation(s_woodVineShader, "u_sapColor");
        s_locSwayAmp = GetShaderLocation(s_woodVineShader, "u_swayAmp");
        s_locTime = GetShaderLocation(s_woodVineShader, "u_time");
        s_locLightDir = GetShaderLocation(s_woodVineShader, "u_lightDir");
        s_woodVineShaderReady = true;
    }
}

const char* VFX_WoodVineCombo_Name(VFX_WoodVineCombo combo)
{
    switch (combo)
    {
        case WOOD_VINE_COMBO_BARE_STEM:       return "BARE STEM (BARK ONLY)";
        case WOOD_VINE_COMBO_THORNY_BRAMBLE:  return "THORNY BRAMBLE (BARK+THORNS)";
        case WOOD_VINE_COMBO_LEAFY_TENDRIL:   return "LEAFY TENDRIL (BARK+THORNS+LEAVES)";
        case WOOD_VINE_COMBO_LOTUS_BLOOM:     return "LOTUS BLOOM (LEAVES+LOTUS)";
        case WOOD_VINE_COMBO_ORCHID_BLOOM:    return "ORCHID BLOOM (LEAVES+ORCHID)";
        case WOOD_VINE_COMBO_PLUM_BLOSSOM:    return "PLUM BLOSSOM (LEAVES+PLUM)";
        case WOOD_VINE_COMBO_FULL_FLOURISH:   return "FULL FLOURISH (ALL ACTIVE)";
        case WOOD_VINE_COMBO_WITHERED_AUTUMN: return "WITHERED AUTUMN (DECAY)";
        case WOOD_VINE_COMBO_AUTO:
        default:                              return "AUTO (DYNAMIC REACTION)";
    }
}

static VFX_WoodVineConfig WoodVine_ResolveCombo(const VFX_WoodVineConfig *inCfg)
{
    VFX_WoodVineConfig eff = *inCfg;
    switch (eff.combo)
    {
        case WOOD_VINE_COMBO_BARE_STEM:
            eff.enableThorns = false;
            eff.enableTwin = false;
            eff.enableLeaves = false;
            eff.enableFlowers = false;
            break;
        case WOOD_VINE_COMBO_THORNY_BRAMBLE:
            eff.enableThorns = true;
            eff.enableTwin = true;
            eff.enableLeaves = false;
            eff.enableFlowers = false;
            break;
        case WOOD_VINE_COMBO_LEAFY_TENDRIL:
            eff.enableThorns = true;
            eff.enableTwin = true;
            eff.enableLeaves = true;
            eff.enableFlowers = false;
            break;
        case WOOD_VINE_COMBO_LOTUS_BLOOM:
            eff.enableThorns = false;
            eff.enableTwin = true;
            eff.enableLeaves = true;
            eff.enableFlowers = true;
            eff.flower.type = WOOD_FLOWER_TYPE_LOTUS;
            break;
        case WOOD_VINE_COMBO_ORCHID_BLOOM:
            eff.enableThorns = false;
            eff.enableTwin = true;
            eff.enableLeaves = true;
            eff.enableFlowers = true;
            eff.flower.type = WOOD_FLOWER_TYPE_ORCHID;
            break;
        case WOOD_VINE_COMBO_PLUM_BLOSSOM:
            eff.enableThorns = false;
            eff.enableTwin = true;
            eff.enableLeaves = true;
            eff.enableFlowers = true;
            eff.flower.type = WOOD_FLOWER_TYPE_PLUM_BLOSSOM;
            break;
        case WOOD_VINE_COMBO_FULL_FLOURISH:
            eff.enableThorns = true;
            eff.enableTwin = true;
            eff.enableLeaves = true;
            eff.enableFlowers = true;
            break;
        case WOOD_VINE_COMBO_WITHERED_AUTUMN:
            eff.enableThorns = true;
            eff.enableTwin = false;
            eff.enableLeaves = true;
            eff.enableFlowers = false;
            eff.wither = (eff.wither > 0.45f) ? eff.wither : 0.65f;
            break;
        case WOOD_VINE_COMBO_AUTO:
        default:
            break;
    }
    return eff;
}

VFX_WoodVineConfig VFX_WoodVine_DefaultConfig(void)
{
    VFX_WoodVineConfig cfg;
    cfg.startPos = (Vector3){0.0f, 0.0f, 0.0f};
    cfg.targetPos = (Vector3){0.0f, 2.2f, 0.0f};
    cfg.targetRadius = 0.0f; // > 0 enables model wrapping
    cfg.targetHeight = 1.8f;
    cfg.length = 3.2f;
    cfg.baseRadius = 0.09f;
    cfg.growth = 1.0f;
    cfg.wither = 0.0f;
    cfg.sapPhase = 0.0f;
    cfg.swayAmp = 0.05f;
    cfg.coilRadius = 0.35f;
    cfg.coilTurns = 2.2f;
    cfg.enableThorns = true;
    cfg.enableTwin = true;
    cfg.enableLeaves = false;
    cfg.leaves = VFX_WoodLeaves_DefaultConfig();
    cfg.enableFlowers = false;
    cfg.flower = VFX_WoodFlower_DefaultConfig();
    cfg.combo = WOOD_VINE_COMBO_AUTO;
    cfg.waterFactor = 0.0f;
    cfg.severArc = 1.0f;
    cfg.fireFactor = 0.0f;
    cfg.castShadow = true;
    cfg.reaction = WOOD_REACTION_NORMAL;
    cfg.variant = WOOD_VINE_VARIANT_SERPENTINE;
    cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
    cfg.seed = 98765;
    return cfg;
}

static const char *s_woodVineVariantDisplayNames[WOOD_VINE_VARIANT_COUNT] = {
    "SERPENTINE", "ENTANGLE WHIP", "HELIX PILLAR", "ANCIENT ROOT", "SEED SPROUT"
};

static const char *s_woodReactionDisplayNames[WOOD_REACTION_COUNT] = {
    "NORMAL DORMANT", "THUY: HYDRATION BLOOM", "KIM: SEVERED CUT", "HOA: EMBER BURNING"
};

int VFX_WoodVine_GetParams(VFX_WoodVineConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int n = 0;

    // 1. Macro Vine parameters [c1, c2, c3, c4...]
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Variant", .group = "Vine", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->variant, .minInt = 0, .maxInt = WOOD_VINE_VARIANT_COUNT - 1,
            .enumNames = s_woodVineVariantDisplayNames, .enumCount = WOOD_VINE_VARIANT_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Style", .group = "Vine", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->style, .minInt = 0, .maxInt = WOOD_VINE_STYLE_COUNT - 1,
            .enumNames = s_woodLeafStyleDisplayNames, .enumCount = WOOD_VINE_STYLE_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Reaction", .group = "Vine", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->reaction, .minInt = 0, .maxInt = WOOD_REACTION_COUNT - 1,
            .enumNames = s_woodReactionDisplayNames, .enumCount = WOOD_REACTION_COUNT
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Thorns", .group = "Vine", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->enableThorns
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Twin Tendril", .group = "Vine", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->enableTwin
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Leaves Active", .group = "Leaves", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->enableLeaves
        };
    }
    if (n < maxParams) {
        outParams[n++] = (VFX_ParamDef){
            .name = "Flowers Active", .group = "Flower", .type = VFX_PARAM_BOOL,
            .valPtr = &cfg->enableFlowers
        };
    }

    // 2. Child A (Leaves) embedded parameters [a1, a2, a3...]
    n += VFX_WoodLeaves_GetParams(&cfg->leaves, outParams + n, maxParams - n);

    // 3. Child B (Flower) embedded parameters [b1, b2, b3...]
    n += VFX_WoodFlower_GetParams(&cfg->flower, outParams + n, maxParams - n);

    return n;
}

const char* VFX_WoodReactionState_Name(VFX_WoodReactionState state)
{
    switch (state)
    {
        case WOOD_REACTION_WATER: return "THUY: HYDRATION BLOOM";
        case WOOD_REACTION_METAL: return "KIM: SEVERED CUT";
        case WOOD_REACTION_FIRE:  return "HOA: EMBER BURNING";
        case WOOD_REACTION_NORMAL:
        default:                  return "NORMAL DORMANT";
    }
}

#define VINE_PATH_MAX_POINTS 24

/* Generates 3D path: Serpentine, Entangle, Spike Spear, Ancient Root, or Seed Sprout */
static int WoodVine_GeneratePath(const VFX_WoodVineConfig *cfg, Vector3 *outPts, int maxPts, float strandOffsetAngle)
{
    int count = (maxPts < VINE_PATH_MAX_POINTS) ? maxPts : VINE_PATH_MAX_POINTS;
    if (count < 6) count = 6;

    Vector3 start = cfg->startPos;
    Vector3 target = cfg->targetPos;

    VFX_WoodVineVariant var = cfg->variant;
    if (cfg->targetRadius > 0.02f) {
        var = WOOD_VINE_VARIANT_ENTANGLE; // Auto-select entangle when wrapping radius is set
    }

    switch (var)
    {
        case WOOD_VINE_VARIANT_ENTANGLE:
        {
            // -----------------------------------------------------------------
            // 1. MODEL WRAPPING: Spirals upward around target cylinder/capsule
            // Always rooted on ground (start.y), never floating in the air.
            // -----------------------------------------------------------------
            float baseY = start.y;
            if (cfg->targetRadius > 0.02f && target.y < baseY) {
                baseY = target.y;
            }
            float hTotal = (cfg->targetHeight > 0.3f) ? cfg->targetHeight : 1.8f;
            if (target.y > baseY + 0.5f && cfg->targetHeight <= 0.3f) {
                hTotal = target.y - baseY;
            }
            float turns = (cfg->coilTurns > 0.5f) ? cfg->coilTurns : 2.2f;
            float startAngle = strandOffsetAngle + ((float)(cfg->seed % 360) * DEG2RAD);

            // Constriction tightness: tightens down as growth progresses
            float tighten = 0.035f * cfg->growth;

            // Target wrapping radius or fallback free-coil radius
            float baseCoilR = (cfg->targetRadius > 0.02f) 
                ? cfg->targetRadius 
                : ((cfg->coilRadius > 0.05f) ? cfg->coilRadius : 0.35f);

            bool hasTarget = (cfg->targetRadius > 0.02f);
            float targetX = hasTarget ? target.x : start.x;
            float targetZ = hasTarget ? target.z : start.z;

            for (int i = 0; i < count; i++)
            {
                float t = (float)i / (float)(count - 1);
                float y = baseY + t * hTotal;
                float theta = startAngle + t * (turns * 2.0f * PI);
                float rHug = baseCoilR + cfg->baseRadius * 0.85f - tighten;
                rHug += 0.025f * sinf(3.0f * theta + t * 4.0f);
                if (rHug < baseCoilR * 0.88f) rHug = baseCoilR * 0.88f;

                // If rooted away from target, blend center smoothly from startPos to targetPos
                float blend = WoodVine_Smoothstep(0.0f, 0.20f, t);
                float cx = start.x + (targetX - start.x) * blend;
                float cz = start.z + (targetZ - start.z) * blend;

                outPts[i] = (Vector3){
                    cx + cosf(theta) * rHug,
                    y,
                    cz + sinf(theta) * rHug
                };
            }
            break;
        }

        case WOOD_VINE_VARIANT_SPIKE_SPEAR:
        {
            // -----------------------------------------------------------------
            // 2. PIERCING BRAMBLE SPEAR: Explosive straight javelin/lance
            // -----------------------------------------------------------------
            Vector3 spearDir = Vector3Subtract(target, start);
            float dist = Vector3Length(spearDir);
            if (dist < 1e-4f) {
                spearDir = (Vector3){0.0f, 1.0f, 0.0f};
                dist = 2.8f;
            } else {
                spearDir = Vector3Scale(spearDir, 1.0f / dist);
            }

            Vector3 ref = (fabsf(spearDir.y) < 0.90f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
            Vector3 side = Vector3Normalize(Vector3CrossProduct(ref, spearDir));

            for (int i = 0; i < count; i++)
            {
                float t = (float)i / (float)(count - 1);
                Vector3 p = Vector3Add(start, Vector3Scale(spearDir, dist * t));

                // Minimal ballistic waviness, sharp rigid piercing arrow
                float jitter = 0.025f * sinf(t * 8.0f + strandOffsetAngle) * (1.0f - t * 0.7f);
                p = Vector3Add(p, Vector3Scale(side, jitter));
                outPts[i] = p;
            }
            break;
        }

        case WOOD_VINE_VARIANT_ANCIENT_ROOT:
        {
            // -----------------------------------------------------------------
            // 3. ANCIENT GROUND ROOT: Heavy crawler hugging terrain contour
            // -----------------------------------------------------------------
            Vector3 crawlDir = Vector3Subtract(target, start);
            crawlDir.y = 0.0f; // Horizontal crawling on ground
            float dist = Vector3Length(crawlDir);
            if (dist < 1e-4f) {
                crawlDir = (Vector3){1.0f, 0.0f, 0.0f};
                dist = 3.5f;
            } else {
                crawlDir = Vector3Scale(crawlDir, 1.0f / dist);
            }

            Vector3 crawlSide = (Vector3){-crawlDir.z, 0.0f, crawlDir.x};
            unsigned int rng = cfg->seed + (unsigned int)(strandOffsetAngle * 50.0f);

            for (int i = 0; i < count; i++)
            {
                float t = (float)i / (float)(count - 1);
                rng = rng * 1664525u + 1013904223u;
                float sideWiggle = ((float)(rng & 0xFFFF) / 65535.0f - 0.5f) * 0.40f * sinf(t * PI);

                float px = start.x + crawlDir.x * (dist * t) + crawlSide.x * sideWiggle;
                float pz = start.z + crawlDir.z * (dist * t) + crawlSide.z * sideWiggle;
                float gY = MapManager_GetGroundHeightAt(px, pz);

                // Small knuckles poking up along crawling root
                float knuckle = 0.045f * sinf(t * 12.0f + strandOffsetAngle) * (1.0f - t * 0.4f);
                if (knuckle < 0.0f) knuckle = 0.0f;

                outPts[i] = (Vector3){px, gY + 0.025f + knuckle, pz};
            }
            break;
        }

        case WOOD_VINE_VARIANT_SEED_SPROUT:
        {
            // -----------------------------------------------------------------
            // 4. SEED IMPACT SPROUT: Rapid uncurling crook / apical hook
            // -----------------------------------------------------------------
            float hMax = (target.y > start.y + 0.5f) ? (target.y - start.y) : 2.0f;
            float curlRadius = 0.32f;
            float curlAngle = strandOffsetAngle + ((float)(cfg->seed % 360) * DEG2RAD);

            for (int i = 0; i < count; i++)
            {
                float t = (float)i / (float)(count - 1);
                float y = start.y + t * hMax;

                // Botanical apical hook: uncoils as growth reaches completion
                float crookPhase = t * 1.5f * PI;
                float crookStrength = WoodVine_Smoothstep(0.35f, 1.0f, t);

                float hookX = sinf(crookPhase) * (curlRadius * crookStrength);
                float hookZ = (1.0f - cosf(crookPhase)) * (curlRadius * crookStrength);

                float cosA = cosf(curlAngle);
                float sinA = sinf(curlAngle);

                outPts[i] = (Vector3){
                    start.x + (hookX * cosA - hookZ * sinA),
                    y - hookZ * 0.30f,
                    start.z + (hookX * sinA + hookZ * cosA)
                };
            }
            break;
        }

        case WOOD_VINE_VARIANT_SERPENTINE:
        default:
        {
            // -----------------------------------------------------------------
            // 5. FREE SERPENTINE MODE: Snake-like reaching tendril with curl noise
            // -----------------------------------------------------------------
            Vector3 dir = Vector3Subtract(target, start);
            float dist = Vector3Length(dir);
            if (dist < 1e-4f) {
                dir = (Vector3){0.0f, 1.0f, 0.0f};
                dist = 2.5f;
            } else {
                dir = Vector3Scale(dir, 1.0f / dist);
            }

            Vector3 ref = (fabsf(dir.y) < 0.90f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
            Vector3 side = Vector3Normalize(Vector3CrossProduct(ref, dir));
            Vector3 up = Vector3CrossProduct(dir, side);

            unsigned int rng = cfg->seed + (unsigned int)(strandOffsetAngle * 100.0f);
            float turns = (cfg->coilTurns > 0.1f) ? cfg->coilTurns : 1.6f;

            for (int i = 0; i < count; i++)
            {
                float t = (float)i / (float)(count - 1);
                Vector3 pAxis = Vector3Add(start, Vector3Scale(dir, dist * t));

                float archY = 0.45f * dist * sinf(t * PI);
                float angle = strandOffsetAngle + t * (turns * 2.0f * PI);
                float rCoil = cfg->coilRadius * (1.0f - 0.25f * t);

                rng = rng * 1664525u + 1013904223u;
                float wiggle = ((float)(rng & 0xFFFF) / 65535.0f - 0.5f) * 0.08f * sinf(t * PI);

                Vector3 radial = Vector3Add(Vector3Scale(side, cosf(angle) * (rCoil + wiggle)),
                                            Vector3Scale(up, archY + sinf(angle) * (rCoil * 0.7f)));
                outPts[i] = Vector3Add(pAxis, radial);
            }
            break;
        }
    }

    return count;
}

/* Draws hooked phyllotaxis thorns with botanical ironwood coloration and facet lighting */
static void WoodVine_DrawThorns(const PMRmfTubeMesh *mesh, float growth, float wither, VFX_WoodVineStyle style, bool depthOnly)
{
    if (mesh == NULL || mesh->segments < 4 || growth <= 0.05f) return;

    const int thornCount = 14;
    const float goldenAngle = 137.507764f * DEG2RAD;

    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());

    rlBegin(RL_TRIANGLES);

    for (int i = 1; i < thornCount; i++)
    {
        float tBirth = (float)i / (float)thornCount;
        if (growth < tBirth) continue;

        // Overshoot pop animation (easeOutBack)
        float u = (growth - tBirth) / 0.14f;
        if (u > 1.0f) u = 1.0f;
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        float popScale = 1.0f + c3 * powf(u - 1.0f, 3.0f) + c1 * powf(u - 1.0f, 2.0f);
        if (popScale < 0.0f) popScale = 0.0f;

        // Map thorn index to spine segment
        int segIdx = (int)(tBirth * (float)mesh->segments);
        if (segIdx >= mesh->segments) segIdx = mesh->segments - 1;

        Vector3 center = mesh->centers[segIdx];
        Vector3 spineTan = Vector3Normalize(Vector3Subtract(mesh->centers[segIdx + 1], mesh->centers[segIdx]));
        Vector3 right = Vector3Normalize(Vector3CrossProduct((fabsf(spineTan.y) < 0.9f ? (Vector3){0,1,0} : (Vector3){1,0,0}), spineTan));
        Vector3 up = Vector3CrossProduct(spineTan, right);

        // Rotate normal around spine by phyllotaxis golden angle
        float phi = (float)i * goldenAngle;
        Vector3 radDir = Vector3Normalize(Vector3Add(Vector3Scale(right, cosf(phi)), Vector3Scale(up, sinf(phi))));

        // Hooked thorn orientation: emerges radially, then curves backward along spine
        float vineR = Vector3Distance(mesh->rings[segIdx][0], center);
        Vector3 thornBase = Vector3Add(center, Vector3Scale(radDir, vineR * 0.92f));

        // Thorn dimensions
        float thornLen = 0.085f * popScale * (1.0f - wither * 0.3f);
        float thornR = 0.024f * popScale;
        
        // Hooked tip: offset radially and backwards against spine growth direction
        Vector3 thornTip = Vector3Add(thornBase, Vector3Add(
            Vector3Scale(radDir, thornLen * 0.85f),
            Vector3Scale(spineTan, -thornLen * 0.45f) // genuine botanical backward-curving hook
        ));

        // Lateral tangent for diamond base collar
        Vector3 tSide = Vector3Normalize(Vector3CrossProduct(radDir, spineTan));

        // 4 base diamond vertices (collar): elongated along spineTan
        Vector3 b[4];
        b[0] = Vector3Add(thornBase, Vector3Scale(spineTan,  thornR * 1.35f)); // front base
        b[1] = Vector3Add(thornBase, Vector3Scale(tSide,     thornR * 0.85f)); // right base
        b[2] = Vector3Add(thornBase, Vector3Scale(spineTan, -thornR * 1.35f)); // rear base
        b[3] = Vector3Add(thornBase, Vector3Scale(tSide,    -thornR * 0.85f)); // left base

        // Style-adapted botanical thorn palette
        Color baseCol;
        Color tipCol;
        switch (style)
        {
            case WOOD_VINE_STYLE_BLOOD_BRAMBLE:
                baseCol = (Color){48, 20, 20, 255};   // dark mahogany collar
                tipCol  = (Color){215, 38, 50, 255};  // sharp ruby crimson needle
                break;
            case WOOD_VINE_STYLE_GOLDEN_AMBER:
                baseCol = (Color){38, 28, 18, 255};   // dark oak collar
                tipCol  = (Color){220, 180, 110, 255}; // ivory / amber bone tip
                break;
            case WOOD_VINE_STYLE_WITHER_GHOST:
                baseCol = (Color){65, 62, 68, 255};   // silver bark collar
                tipCol  = (Color){175, 120, 215, 255}; // spectral violet needle
                break;
            case WOOD_VINE_STYLE_JADE_EMERALD:
            default:
                baseCol = (Color){26, 22, 18, 255};   // pitch ironwood collar
                tipCol  = (Color){35, 225, 135, 255}; // sharp obsidian with jade toxin gleam
                break;
        }

        for (int k = 0; k < 4; k++)
        {
            Vector3 b1 = b[k];
            Vector3 b2 = b[(k + 1) % 4];

            Vector3 triN = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b2, b1), Vector3Subtract(thornTip, b1)));

            if (!depthOnly)
            {
                float NdotL = fmaxf(Vector3DotProduct(triN, sunDir), 0.0f);
                float diffuse = 0.30f + 0.70f * NdotL;

                Color cBase = (Color){(unsigned char)(baseCol.r * diffuse), (unsigned char)(baseCol.g * diffuse), (unsigned char)(baseCol.b * diffuse), 255};
                Color cTip  = (Color){(unsigned char)(tipCol.r * diffuse), (unsigned char)(tipCol.g * diffuse), (unsigned char)(tipCol.b * diffuse), 255};

                rlColor4ub(cBase.r, cBase.g, cBase.b, 255);
                rlNormal3f(triN.x, triN.y, triN.z);
                rlVertex3f(b1.x, b1.y, b1.z);

                rlColor4ub(cBase.r, cBase.g, cBase.b, 255);
                rlNormal3f(triN.x, triN.y, triN.z);
                rlVertex3f(b2.x, b2.y, b2.z);

                rlColor4ub(cTip.r, cTip.g, cTip.b, 255);
                rlNormal3f(triN.x, triN.y, triN.z);
                rlVertex3f(thornTip.x, thornTip.y, thornTip.z);
            }
            else
            {
                rlNormal3f(triN.x, triN.y, triN.z);
                rlVertex3f(b1.x, b1.y, b1.z);
                rlVertex3f(b2.x, b2.y, b2.z);
                rlVertex3f(thornTip.x, thornTip.y, thornTip.z);
            }
        }
    }

    rlEnd();
}

/* Draws continuous soft contact shadow ribbon on ground (fallback when real shadow maps are disabled) */
static void WoodVine_DrawContactShadowRibbon(const PMRmfTubeMesh *mesh, float growth, float baseRadius)
{
    if (mesh == NULL || growth <= 0.02f) return;

    int activeRings = (int)(growth * (float)mesh->segments);
    if (activeRings > mesh->segments) activeRings = mesh->segments;
    if (activeRings < 2) return;

    Vector3 sunDir = Vector3Normalize(Environment_GetSunDirection());
    float sunY = sunDir.y;
    if (sunY > -0.15f) sunY = -0.55f; // ensure downward projection

    rlPushMatrix();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    rlCheckRenderBatchLimit((activeRings + 1) * 6);
    rlBegin(RL_QUADS);

    for (int i = 0; i < activeRings; i++)
    {
        Vector3 c1 = mesh->centers[i];
        Vector3 c2 = mesh->centers[i + 1];

        float gY1 = MapManager_GetGroundHeightAt(c1.x, c1.z);
        float gY2 = MapManager_GetGroundHeightAt(c2.x, c2.z);

        float h1 = c1.y - gY1;
        float h2 = c2.y - gY2;
        if (h1 < 0.0f) h1 = 0.0f;
        if (h2 < 0.0f) h2 = 0.0f;
        if (h1 > 3.5f && h2 > 3.5f) continue; // too high for ground contact shadow

        // Project center points to ground along sun direction
        float t1 = h1 / (-sunY);
        float t2 = h2 / (-sunY);
        Vector3 p1 = (Vector3){c1.x + sunDir.x * t1, gY1 + 0.005f, c1.z + sunDir.z * t1};
        Vector3 p2 = (Vector3){c2.x + sunDir.x * t2, gY2 + 0.005f, c2.z + sunDir.z * t2};

        Vector3 segDir = Vector3Subtract(p2, p1);
        float segLen = Vector3Length(segDir);
        if (segLen < 1e-4f) continue;
        Vector3 segTan = Vector3Scale(segDir, 1.0f / segLen);
        Vector3 segRight = (Vector3){-segTan.z, 0.0f, segTan.x};

        float frac1 = (float)i / (float)mesh->segments;
        float frac2 = (float)(i + 1) / (float)mesh->segments;

        float w1 = (baseRadius * 1.5f * (1.0f - frac1 * 0.45f)) * (1.0f + h1 * 0.20f);
        float w2 = (baseRadius * 1.5f * (1.0f - frac2 * 0.45f)) * (1.0f + h2 * 0.20f);

        // Alpha softens as vine ascends into air
        float a1 = 0.58f * (1.0f - h1 / 3.5f);
        float a2 = 0.58f * (1.0f - h2 / 3.5f);
        if (a1 < 0.0f) a1 = 0.0f;
        if (a2 < 0.0f) a2 = 0.0f;

        unsigned char alpha1 = (unsigned char)(a1 * 255.0f);
        unsigned char alpha2 = (unsigned char)(a2 * 255.0f);

        Vector3 left1  = Vector3Add(p1, Vector3Scale(segRight, -w1));
        Vector3 right1 = Vector3Add(p1, Vector3Scale(segRight,  w1));
        Vector3 left2  = Vector3Add(p2, Vector3Scale(segRight, -w2));
        Vector3 right2 = Vector3Add(p2, Vector3Scale(segRight,  w2));

        rlColor4ub(10, 8, 6, alpha1);
        rlVertex3f(left1.x, left1.y, left1.z);
        rlColor4ub(10, 8, 6, alpha2);
        rlVertex3f(left2.x, left2.y, left2.z);
        rlColor4ub(10, 8, 6, alpha2);
        rlVertex3f(right2.x, right2.y, right2.z);
        rlColor4ub(10, 8, 6, alpha1);
        rlVertex3f(right1.x, right1.y, right1.z);
    }

    rlEnd();
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlPopMatrix();
}

void VFX_ComposeWoodVine(const VFX_WoodVineConfig *config)
{
    if (config == NULL || config->growth <= 0.001f) return;

    VFX_WoodVineConfig eff = WoodVine_ResolveCombo(config);

    // Safety: default severArc to 1.0f (unsevered) if not specified or zero
    if (eff.severArc <= 0.001f) eff.severArc = 1.0f;

    // -------------------------------------------------------------------------
    // WUXING ELEMENTAL REACTION OVERRIDES
    // -------------------------------------------------------------------------
    switch (eff.reaction)
    {
        case WOOD_REACTION_WATER: // Thủy sinh Mộc: Hydration bloom (leaves, flowers, rapid sap)
            eff.enableLeaves = true;
            // Only auto-hydrate if caller left it 0 and vine is already grown
            if (eff.waterFactor <= 0.001f && eff.growth >= 0.99f) eff.waterFactor = 1.0f;
            eff.severArc = 1.0f; // Intact, never severed or collapsed
            break;
        case WOOD_REACTION_METAL: // Kim khắc Mộc: Severed cut (cut tip drops, stump withers)
            if (eff.severArc >= 0.999f) eff.severArc = 0.55f;
            eff.wither = fmaxf(eff.wither, 0.65f);
            break;
        case WOOD_REACTION_FIRE:  // Hỏa thiêu Mộc: Combustion (charcoal bark, magma embers, ash)
            if (eff.fireFactor <= 0.001f) eff.fireFactor = 1.0f;
            eff.wither = fmaxf(eff.wither, 0.88f);
            eff.severArc = 1.0f; // Intact, never severed or collapsed
            break;
        case WOOD_REACTION_NORMAL:
        default:
            eff.severArc = 1.0f; // Intact, never severed or collapsed
            break;
    }

    // -------------------------------------------------------------------------
    // 1. GENERATE MAIN VINE PATH & MESH
    // -------------------------------------------------------------------------
    Vector3 pathMain[VINE_PATH_MAX_POINTS];
    int pathCount = WoodVine_GeneratePath(&eff, pathMain, VINE_PATH_MAX_POINTS, 0.0f);

    float effectiveGrowth = eff.growth;
    if (eff.severArc < 0.999f)
    {
        effectiveGrowth = fminf(eff.growth, eff.severArc);
    }

    PMRmfTubeConfig tubeCfg = PMRmfTube_DefaultConfig();
    tubeCfg.baseRadius = eff.baseRadius;
    tubeCfg.taperPow = 1.15f;
    tubeCfg.tipRadiusFrac = (eff.severArc < 0.999f && eff.growth >= eff.severArc) ? 0.35f : 0.06f; // blunt cut edge
    tubeCfg.rootFlare = 0.50f;
    tubeCfg.knotAmp = 0.18f;
    tubeCfg.knotFreq = 7.5f;
    tubeCfg.segments = 28;
    tubeCfg.radialSegs = 8;
    tubeCfg.growth = effectiveGrowth;
    tubeCfg.birth = 0.0f;
    tubeCfg.span = 1.0f;
    tubeCfg.tipLength = 0.08f;
    tubeCfg.swayAmp = (eff.fireFactor > 0.05f) ? eff.swayAmp * 0.4f : eff.swayAmp;

    // Tailor tube morphology to variant
    if (eff.variant == WOOD_VINE_VARIANT_SPIKE_SPEAR) {
        tubeCfg.taperPow = 1.55f;
        tubeCfg.tipRadiusFrac = 0.012f;
        tubeCfg.rootFlare = 0.70f;
        tubeCfg.swayAmp = eff.swayAmp * 0.20f;
    } else if (eff.variant == WOOD_VINE_VARIANT_ANCIENT_ROOT) {
        tubeCfg.baseRadius = eff.baseRadius * 1.45f;
        tubeCfg.knotAmp = 0.25f;
        tubeCfg.rootFlare = 0.85f;
        tubeCfg.swayAmp = 0.0f;
    } else if (eff.variant == WOOD_VINE_VARIANT_SEED_SPROUT) {
        tubeCfg.baseRadius = eff.baseRadius * 0.82f;
        tubeCfg.tipRadiusFrac = 0.08f;
    }

    PMRmfTubeMesh meshMain;
    PMRmf_BuildTube(pathMain, pathCount, &tubeCfg, &meshMain);

    // -------------------------------------------------------------------------
    // 1b. SEVERED FALLING PIECE (Kim khắc Mộc physics tumble)
    // -------------------------------------------------------------------------
    int cutIdx = (int)(eff.severArc * (float)(pathCount - 1));
    if (cutIdx < 1) cutIdx = 1;
    if (cutIdx > pathCount - 2) cutIdx = pathCount - 2;

    int sevCount = pathCount - cutIdx;
    Vector3 sevPath[VINE_PATH_MAX_POINTS];
    bool hasSeveredPiece = (eff.severArc < 0.999f && eff.growth >= eff.severArc && sevCount >= 2);
    PMRmfTubeMesh meshSevered;

    if (hasSeveredPiece)
    {
        float fallProg = (eff.growth >= 0.999f)
            ? fminf(eff.wither * 1.5f, 1.0f)
            : ((eff.growth - eff.severArc) / (1.0f - eff.severArc));
        if (fallProg < 0.0f) fallProg = 0.0f;
        if (fallProg > 1.0f) fallProg = 1.0f;

        float dropSec = fallProg * 1.25f;
        float dropY = 0.5f * 9.81f * dropSec * dropSec;
        float tumble = fallProg * 2.5f;
        float floorY = (eff.targetRadius > 0.02f) ? eff.startPos.y : 0.0f;

        for (int i = 0; i < sevCount; i++)
        {
            Vector3 pt = pathMain[cutIdx + i];
            pt.y -= dropY;
            if (pt.y < floorY + 0.03f) pt.y = floorY + 0.03f;
            pt.x += sinf(tumble + (float)i * 0.15f) * (0.08f * fallProg);
            pt.z += cosf(tumble + (float)i * 0.15f) * (0.08f * fallProg);
            sevPath[i] = pt;
        }

        PMRmfTubeConfig sevTubeCfg = tubeCfg;
        sevTubeCfg.baseRadius = eff.baseRadius * (1.0f - eff.severArc * 0.45f);
        sevTubeCfg.growth = 1.0f;
        sevTubeCfg.swayAmp = 0.0f;
        PMRmf_BuildTube(sevPath, sevCount, &sevTubeCfg, &meshSevered);
    }

    // -------------------------------------------------------------------------
    // 2. DIRECTIONAL SHADOW MAP PRE-PASS (Real Shading P6)
    // -------------------------------------------------------------------------
    bool isShadowPass = EnvShadow_IsCapturing();
    if (isShadowPass)
    {
        PMRmf_Draw(&meshMain, WHITE, 1.0f, 0.0f);
        if (hasSeveredPiece)
        {
            PMRmf_Draw(&meshSevered, WHITE, 1.0f, 0.0f);
        }
        if (eff.enableTwin && eff.severArc >= 0.999f)
        {
            Vector3 pathTwin[VINE_PATH_MAX_POINTS];
            WoodVine_GeneratePath(&eff, pathTwin, VINE_PATH_MAX_POINTS, 2.1f);

            PMRmfTubeConfig twinCfg = tubeCfg;
            twinCfg.baseRadius = eff.baseRadius * 0.45f;
            twinCfg.taperPow = 1.3f;
            twinCfg.knotFreq = 12.0f;
            twinCfg.segments = 24;
            twinCfg.radialSegs = 6;
            twinCfg.seed = eff.seed + 555;

            PMRmfTubeMesh meshTwin;
            PMRmf_BuildTube(pathTwin, pathCount, &twinCfg, &meshTwin);
            PMRmf_Draw(&meshTwin, WHITE, 1.2f, 0.5f);
        }
        if (eff.enableThorns)
        {
            WoodVine_DrawThorns(&meshMain, effectiveGrowth, eff.wither, eff.style, true);
            if (hasSeveredPiece)
            {
                WoodVine_DrawThorns(&meshSevered, 1.0f, eff.wither + 0.3f, eff.style, true);
            }
        }
        if (eff.enableLeaves || eff.enableFlowers || eff.waterFactor > 0.05f)
        {
            #define MAX_VINE_LEAF_SOCKETS 32
            VFX_BotanicalSocket leafSockets[MAX_VINE_LEAF_SOCKETS];
            int leafCount = Botanical_SampleSocketsOnSpine(pathMain, pathCount, eff.baseRadius,
                                                          leafSockets, MAX_VINE_LEAF_SOCKETS,
                                                          eff.seed + 777, 0.22f);
            if (leafCount > 0)
            {
                if (eff.severArc < 0.999f)
                {
                    int kept = 0;
                    for (int i = 0; i < leafCount; i++) {
                        if (leafSockets[i].arc <= eff.severArc) leafSockets[kept++] = leafSockets[i];
                    }
                    leafCount = kept;
                }

                if (leafCount > 0)
                {
                    float leafProg = (eff.waterFactor > 0.001f)
                        ? WoodVine_Smoothstep(0.0f, 0.65f, eff.waterFactor)
                        : (eff.enableLeaves ? 1.0f : 0.0f);
                    float flowerProg = (eff.waterFactor > 0.001f)
                        ? WoodVine_Smoothstep(0.25f, 1.0f, eff.waterFactor)
                        : (eff.enableFlowers ? 1.0f : 0.0f);

                    if (leafProg > 0.001f)
                    {
                        VFX_WoodLeavesConfig leafCfg = eff.leaves;
                        leafCfg.attached = true;
                        leafCfg.sockets = leafSockets;
                        leafCfg.socketCount = leafCount;
                        leafCfg.growth = effectiveGrowth * leafProg;
                        leafCfg.wither = eff.wither;
                        leafCfg.size = (eff.waterFactor > 0.05f) ? (0.16f + 0.05f * eff.waterFactor) : 0.15f;
                        leafCfg.seed = eff.seed + 101;
                        VFX_ComposeWoodLeaves(&leafCfg);
                    }

                    if ((eff.waterFactor > 0.05f || eff.enableFlowers) && flowerProg > 0.001f)
                    {
                        VFX_BotanicalSocket flowerSockets[8];
                        int flowerCount = 0;
                        for (int i = 0; i < leafCount && flowerCount < 8; i++) {
                            if (i % 3 == 1) flowerSockets[flowerCount++] = leafSockets[i];
                        }
                        if (flowerCount > 0)
                        {
                            VFX_WoodFlowerConfig flwCfg = eff.flower;
                            flwCfg.attached = true;
                            flwCfg.sockets = flowerSockets;
                            flwCfg.socketCount = flowerCount;
                            flwCfg.growth = effectiveGrowth * flowerProg;
                            flwCfg.wither = eff.wither;
                            flwCfg.seed = eff.seed + 202;
                            VFX_ComposeWoodFlower(&flwCfg);
                        }
                    }
                }
            }
        }
        return; // Shadow depth pass completed
    }

    // -------------------------------------------------------------------------
    // 3. GROUND SHADOW FALLBACK
    // -------------------------------------------------------------------------
    if (eff.castShadow && !EnvShadow_IsEnabled())
    {
        WoodVine_DrawContactShadowRibbon(&meshMain, effectiveGrowth, eff.baseRadius);
    }

    // -------------------------------------------------------------------------
    // 4. RENDER MAIN VINE WITH BOTANICAL SHADER
    // -------------------------------------------------------------------------
    WoodVine_InitShader();

    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    if (s_woodVineShaderReady && s_woodVineShader.id > 0)
    {
        BeginShaderMode(s_woodVineShader);
        SkillManager_BeginShader(s_woodVineShader);

        float g = effectiveGrowth;
        float w = eff.wither;
        float freq = (eff.fireFactor > 0.05f) ? 8.5f : ((eff.waterFactor > 0.05f) ? 5.2f : 4.0f);
        float speed = (eff.fireFactor > 0.05f) ? 5.0f : ((eff.waterFactor > 0.05f) ? 4.2f : 2.5f);
        float sway = (eff.fireFactor > 0.05f) ? eff.swayAmp * 0.4f : eff.swayAmp;
        float time = (float)GetTime();

        Vector3 sunDir = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
        float lightDir[3] = {sunDir.x, sunDir.y, sunDir.z};

        float baseCol[4];
        float foliageCol[4];
        float sapCol[4];

        if (eff.fireFactor > 0.05f)
        {
            // Hỏa thiêu Mộc: Charcoal black bark with smoldering liquid magma veins
            baseCol[0] = 0.06f; baseCol[1] = 0.05f; baseCol[2] = 0.05f; baseCol[3] = 1.0f;
            foliageCol[0] = 0.16f; foliageCol[1] = 0.09f; foliageCol[2] = 0.05f; foliageCol[3] = 1.0f;
            sapCol[0] = 3.80f; sapCol[1] = 1.10f; sapCol[2] = 0.15f; sapCol[3] = 1.0f;
        }
        else
        {
            switch (eff.style)
            {
                case WOOD_VINE_STYLE_BLOOD_BRAMBLE:
                    baseCol[0] = 0.30f; baseCol[1] = 0.11f; baseCol[2] = 0.10f; baseCol[3] = 1.0f;
                    foliageCol[0] = 0.88f; foliageCol[1] = 0.16f; foliageCol[2] = 0.22f; foliageCol[3] = 1.0f;
                    sapCol[0] = 2.20f; sapCol[1] = 0.22f; sapCol[2] = 0.35f; sapCol[3] = 1.0f;
                    break;
                case WOOD_VINE_STYLE_GOLDEN_AMBER:
                    baseCol[0] = 0.26f; baseCol[1] = 0.18f; baseCol[2] = 0.11f; baseCol[3] = 1.0f;
                    foliageCol[0] = 0.92f; foliageCol[1] = 0.68f; foliageCol[2] = 0.15f; foliageCol[3] = 1.0f;
                    sapCol[0] = 2.10f; sapCol[1] = 1.35f; sapCol[2] = 0.20f; sapCol[3] = 1.0f;
                    break;
                case WOOD_VINE_STYLE_WITHER_GHOST:
                    baseCol[0] = 0.36f; baseCol[1] = 0.36f; baseCol[2] = 0.34f; baseCol[3] = 1.0f;
                    foliageCol[0] = 0.65f; foliageCol[1] = 0.45f; foliageCol[2] = 0.80f; foliageCol[3] = 1.0f;
                    sapCol[0] = 1.50f; sapCol[1] = 0.35f; sapCol[2] = 2.10f; sapCol[3] = 1.0f;
                    break;
                case WOOD_VINE_STYLE_JADE_EMERALD:
                default:
                    baseCol[0] = (eff.waterFactor > 0.05f) ? 0.20f : 0.24f;
                    baseCol[1] = (eff.waterFactor > 0.05f) ? 0.26f : 0.18f;
                    baseCol[2] = (eff.waterFactor > 0.05f) ? 0.16f : 0.12f;
                    baseCol[3] = 1.0f;
                    foliageCol[0] = (eff.waterFactor > 0.05f) ? 0.15f : 0.10f;
                    foliageCol[1] = (eff.waterFactor > 0.05f) ? 0.98f : 0.90f;
                    foliageCol[2] = (eff.waterFactor > 0.05f) ? 0.55f : 0.45f;
                    foliageCol[3] = 1.0f;
                    sapCol[0] = (eff.waterFactor > 0.05f) ? 0.15f : 0.20f;
                    sapCol[1] = (eff.waterFactor > 0.05f) ? 2.80f : 2.20f;
                    sapCol[2] = (eff.waterFactor > 0.05f) ? 1.70f : 1.20f;
                    sapCol[3] = 1.0f;
                    break;
            }
        }

        SetShaderValue(s_woodVineShader, s_locGrowth, &g, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_woodVineShader, s_locWither, &w, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_woodVineShader, s_locSapPulseFreq, &freq, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_woodVineShader, s_locSapPulseSpeed, &speed, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_woodVineShader, s_locBaseColor, baseCol, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_woodVineShader, s_locFoliageColor, foliageCol, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_woodVineShader, s_locSapColor, sapCol, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_woodVineShader, s_locSwayAmp, &sway, SHADER_UNIFORM_FLOAT);
        if (s_locTime >= 0) SetShaderValue(s_woodVineShader, s_locTime, &time, SHADER_UNIFORM_FLOAT);
        if (s_locLightDir >= 0) SetShaderValue(s_woodVineShader, s_locLightDir, lightDir, SHADER_UNIFORM_VEC3);

        PMRmf_Draw(&meshMain, WHITE, 1.0f, 0.0f);

        // Draw severed piece if detached by metal cut
        if (hasSeveredPiece)
        {
            float sevW = eff.wither + 0.35f;
            SetShaderValue(s_woodVineShader, s_locWither, &sevW, SHADER_UNIFORM_FLOAT);
            PMRmf_Draw(&meshSevered, WHITE, 1.0f, 0.0f);
            SetShaderValue(s_woodVineShader, s_locWither, &w, SHADER_UNIFORM_FLOAT);
        }

        // ---------------------------------------------------------------------
        // 5. BRAIDED TWIN TENDRIL (Secondary coiling strand)
        // ---------------------------------------------------------------------
        if (eff.enableTwin && eff.severArc >= 0.999f)
        {
            Vector3 pathTwin[VINE_PATH_MAX_POINTS];
            WoodVine_GeneratePath(&eff, pathTwin, VINE_PATH_MAX_POINTS, 2.1f);

            PMRmfTubeConfig twinCfg = tubeCfg;
            twinCfg.baseRadius = eff.baseRadius * 0.45f;
            twinCfg.taperPow = 1.3f;
            twinCfg.knotFreq = 12.0f;
            twinCfg.segments = 24;
            twinCfg.radialSegs = 6;
            twinCfg.seed = eff.seed + 555;

            PMRmfTubeMesh meshTwin;
            PMRmf_BuildTube(pathTwin, pathCount, &twinCfg, &meshTwin);
            PMRmf_Draw(&meshTwin, WHITE, 1.2f, 0.5f);
        }

        EndShaderMode();
    }
    else
    {
        PMRmf_Draw(&meshMain, (Color){50, 140, 70, 255}, 1.0f, 0.0f);
        if (hasSeveredPiece) PMRmf_Draw(&meshSevered, (Color){85, 65, 45, 255}, 1.0f, 0.0f);
    }

    // -------------------------------------------------------------------------
    // 6. HOOKED PHYLLOTAXIS THORNS
    // -------------------------------------------------------------------------
    if (eff.enableThorns)
    {
        WoodVine_DrawThorns(&meshMain, effectiveGrowth, eff.wither, eff.style, false);
        if (hasSeveredPiece)
        {
            WoodVine_DrawThorns(&meshSevered, 1.0f, eff.wither + 0.35f, eff.style, false);
        }
    }

    // -------------------------------------------------------------------------
    // 7. ATTACHED BOTANICAL FOLIAGE LEAVES & BLOOMING FLOWERS
    // -------------------------------------------------------------------------
    if (eff.enableLeaves || eff.enableFlowers || eff.waterFactor > 0.05f)
    {
        VFX_BotanicalSocket leafSockets[MAX_VINE_LEAF_SOCKETS];
        int leafCount = Botanical_SampleSocketsOnSpine(pathMain, pathCount, eff.baseRadius,
                                                      leafSockets, MAX_VINE_LEAF_SOCKETS,
                                                      eff.seed + 777, 0.22f);
        if (leafCount > 0)
        {
            if (eff.severArc < 0.999f)
            {
                int kept = 0;
                for (int i = 0; i < leafCount; i++) {
                    if (leafSockets[i].arc <= eff.severArc) leafSockets[kept++] = leafSockets[i];
                }
                leafCount = kept;
            }

            if (leafCount > 0)
            {
                float leafProg = (eff.waterFactor > 0.001f)
                    ? WoodVine_Smoothstep(0.0f, 0.65f, eff.waterFactor)
                    : (eff.enableLeaves ? 1.0f : 0.0f);
                float flowerProg = (eff.waterFactor > 0.001f)
                    ? WoodVine_Smoothstep(0.25f, 1.0f, eff.waterFactor)
                    : (eff.enableFlowers ? 1.0f : 0.0f);

                if (leafProg > 0.001f)
                {
                    VFX_WoodLeavesConfig leafCfg = eff.leaves;
                    leafCfg.attached = true;
                    leafCfg.sockets = leafSockets;
                    leafCfg.socketCount = leafCount;
                    leafCfg.growth = effectiveGrowth * leafProg;
                    leafCfg.wither = eff.wither;
                    leafCfg.swayAmp = eff.swayAmp * 1.35f;
                    leafCfg.size = (eff.waterFactor > 0.05f) ? (0.16f + 0.05f * eff.waterFactor) : 0.15f;
                    leafCfg.seed = eff.seed + 101;
                    VFX_ComposeWoodLeaves(&leafCfg);
                }

                // Bloom flowers on hydration (Thủy sinh Mộc) or explicit enableFlowers
                if ((eff.waterFactor > 0.05f || eff.enableFlowers) && flowerProg > 0.001f)
                {
                    VFX_BotanicalSocket flowerSockets[8];
                    int flowerCount = 0;
                    for (int i = 0; i < leafCount && flowerCount < 8; i++) {
                        if (i % 3 == 1) flowerSockets[flowerCount++] = leafSockets[i];
                    }
                    if (flowerCount > 0)
                    {
                        VFX_WoodFlowerConfig flwCfg = eff.flower;
                        flwCfg.attached = true;
                        flwCfg.sockets = flowerSockets;
                        flwCfg.socketCount = flowerCount;
                        flwCfg.growth = effectiveGrowth * flowerProg;
                        flwCfg.wither = eff.wither;
                        flwCfg.swayAmp = eff.swayAmp * 1.15f;
                        flwCfg.size = 0.13f + 0.03f * eff.waterFactor;
                        flwCfg.seed = eff.seed + 202;
                        VFX_ComposeWoodFlower(&flwCfg);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // 8. RISING EMBER MOTES (Hỏa thiêu Mộc)
    // -------------------------------------------------------------------------
    if (eff.fireFactor > 0.05f)
    {
        float t = (float)GetTime();
        rlBegin(RL_QUADS);
        for (int e = 0; e < 16; e++)
        {
            float ep = fmodf(t * 0.9f + (float)e * 0.187f, 1.0f);
            int seg = (e * 3) % (pathCount - 1);
            Vector3 bPos = Vector3Lerp(pathMain[seg], pathMain[seg + 1], 0.5f);
            Vector3 emberPos = (Vector3){
                bPos.x + sinf(t * 3.5f + (float)e * 1.2f) * 0.07f,
                bPos.y + ep * 1.1f,
                bPos.z + cosf(t * 3.5f + (float)e * 1.2f) * 0.07f
            };
            float sz = 0.016f * (1.0f - ep * 0.5f);
            unsigned char a = (unsigned char)((1.0f - ep) * 235.0f);
            rlColor4ub(255, 140, 25, a);
            rlVertex3f(emberPos.x - sz, emberPos.y - sz, emberPos.z);
            rlVertex3f(emberPos.x + sz, emberPos.y - sz, emberPos.z);
            rlVertex3f(emberPos.x + sz, emberPos.y + sz, emberPos.z);
            rlVertex3f(emberPos.x - sz, emberPos.y + sz, emberPos.z);
        }
        rlEnd();
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
}

void VFX_ComposeWoodVineCluster(Vector3 center, float radius, float height,
                                float growth, float wither, float sapPhase,
                                int vineCount, unsigned int seed)
{
    if (growth <= 0.001f || vineCount <= 0) return;
    if (vineCount > 8) vineCount = 8;
    unsigned int rng = seed;

    for (int i = 0; i < vineCount; i++)
    {
        float angle = ((float)i / (float)vineCount) * 2.0f * PI;
        rng = rng * 1664525u + 1013904223u;
        float rJitter = radius * (0.85f + 0.30f * ((float)(rng & 0xFFFF) / 65535.0f));
        
        Vector3 start = (Vector3){
            center.x + cosf(angle) * rJitter,
            center.y,
            center.z + sinf(angle) * rJitter
        };

        Vector3 target = (Vector3){
            center.x + cosf(angle + 0.5f) * (radius * 0.25f),
            center.y + height,
            center.z + sinf(angle + 0.5f) * (radius * 0.25f)
        };

        VFX_WoodVineConfig cfg = VFX_WoodVine_DefaultConfig();
        cfg.startPos = start;
        cfg.targetPos = target;
        cfg.length = height * 1.2f;
        cfg.baseRadius = 0.08f + 0.02f * ((float)(rng & 0xFF) / 255.0f);
        cfg.growth = growth;
        cfg.wither = wither;
        cfg.sapPhase = sapPhase + (float)i * 0.25f;
        cfg.swayAmp = 0.05f;
        cfg.coilRadius = radius * 0.45f;
        cfg.coilTurns = 1.8f + 0.3f * (float)(i % 2);
        cfg.enableThorns = true;
        cfg.enableTwin = (i % 2 == 0);
        cfg.castShadow = true;
        cfg.variant = (i % 2 == 0) ? WOOD_VINE_VARIANT_SERPENTINE : WOOD_VINE_VARIANT_ENTANGLE;
        cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
        cfg.seed = seed + (unsigned int)(i * 37);

        VFX_ComposeWoodVine(&cfg);
    }
}

/* Spawns an atomic seed impact burst: ground root flare + uncurling vine sprout */
void VFX_ComposeWoodVineSeedSprout(Vector3 impactPos, float progress, unsigned int seed, VFX_WoodVineStyle style)
{
    if (progress <= 0.001f) return;

    // 1. Primary central sprout: uncurls upward with apical crook
    VFX_WoodVineConfig mainCfg = VFX_WoodVine_DefaultConfig();
    mainCfg.startPos = impactPos;
    mainCfg.targetPos = Vector3Add(impactPos, (Vector3){0.0f, 1.8f, 0.0f});
    mainCfg.length = 2.2f;
    mainCfg.baseRadius = 0.075f;
    mainCfg.growth = progress;
    mainCfg.variant = WOOD_VINE_VARIANT_SEED_SPROUT;
    mainCfg.style = style;
    mainCfg.enableThorns = true;
    mainCfg.enableTwin = true;
    mainCfg.castShadow = true;
    mainCfg.seed = seed;
    VFX_ComposeWoodVine(&mainCfg);

    // 2. Secondary side tendril curling outward
    if (progress > 0.15f)
    {
        float p2 = (progress - 0.15f) / 0.85f;
        VFX_WoodVineConfig sideCfg = mainCfg;
        sideCfg.startPos = impactPos;
        sideCfg.targetPos = Vector3Add(impactPos, (Vector3){0.65f, 0.95f, 0.45f});
        sideCfg.length = 1.3f;
        sideCfg.baseRadius = 0.045f;
        sideCfg.growth = p2;
        sideCfg.variant = WOOD_VINE_VARIANT_SERPENTINE;
        sideCfg.coilRadius = 0.22f;
        sideCfg.enableTwin = false;
        sideCfg.seed = seed + 777;
        VFX_ComposeWoodVine(&sideCfg);
    }
}

const char* VFX_WoodVineVariant_Name(VFX_WoodVineVariant variant)
{
    switch (variant)
    {
        case WOOD_VINE_VARIANT_SERPENTINE:  return "SERPENTINE TENDRIL";
        case WOOD_VINE_VARIANT_ENTANGLE:    return "DEMON ENTANGLE";
        case WOOD_VINE_VARIANT_SPIKE_SPEAR: return "BRAMBLE SPIKE SPEAR";
        case WOOD_VINE_VARIANT_ANCIENT_ROOT:return "ANCIENT GROUND ROOT";
        case WOOD_VINE_VARIANT_SEED_SPROUT: return "SEED IMPACT SPROUT";
        default: return "UNKNOWN";
    }
}

const char* VFX_WoodVineStyle_Name(VFX_WoodVineStyle style)
{
    switch (style)
    {
        case WOOD_VINE_STYLE_JADE_EMERALD: return "CELESTIAL JADE (Cyan Veins)";
        case WOOD_VINE_STYLE_BLOOD_BRAMBLE:return "BLOOD BRAMBLE (Ruby Veins)";
        case WOOD_VINE_STYLE_GOLDEN_AMBER: return "ANCIENT IRONWOOD (Amber Veins)";
        case WOOD_VINE_STYLE_WITHER_GHOST: return "GHOST TIMBER (Violet Veins)";
        default: return "UNKNOWN";
    }
}
