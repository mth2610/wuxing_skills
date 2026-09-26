// Batched, shader-driven meadow/flower/water surfaces. All geometry is built
// once at map init and submitted as a small number of opaque draw calls.

static Shader s_natureOpaqueShader = {0};
static Shader s_natureCutoutShader = {0};
static Shader s_natureShadowShader = {0};
static Shader s_flowerShadowShader = {0};
static Shader s_waterShader = {0};
static bool s_natureOpaqueShaderReady = false;
static bool s_natureCutoutShaderReady = false;
static bool s_natureShadowShaderReady = false;
static bool s_flowerShadowShaderReady = false;
static bool s_waterShaderReady = false;
static Texture2D s_defaultCausticTex = {0};

static int s_waterLocTime = -1;
static int s_waterLocWaveHeight = -1;
static int s_waterLocWaveScale = -1;
static int s_waterLocWaveSpeed = -1;
static int s_waterLocDetailScale = -1;
static int s_waterLocDetailStrength = -1;
static int s_waterLocFlowVelocity = -1;
static int s_waterLocWaterShape = -1;
static int s_waterLocLightDir = -1;
static int s_waterLocLightColor = -1;
static int s_waterLocAmbientColor = -1;
static int s_waterLocViewPos = -1;
static int s_waterLocMaxDepth = -1;
static int s_waterLocAbsorption = -1;
static int s_waterLocDeepColor = -1;
static int s_waterLocShallowColor = -1;
static int s_waterLocFoamColor = -1;
static int s_waterLocScatterColor = -1;
static int s_waterLocScatterCoeff = -1;
static int s_waterLocCausticsStrength = -1;
static int s_waterLocCausticsScale = -1;
static int s_waterLocFoamThreshold = -1;
static int s_waterLocCausticTex = -1;
static int s_waterLocCameraDepthTex = -1;
static int s_waterLocHasDepthTex = -1;
static int s_waterLocModelPos = -1;
static int s_waterLocResolution = -1;
static int s_waterLocInteractor = -1;
static int s_waterLocVelocity = -1;
static int s_waterLocRadius = -1;
static int s_waterLocRippleRings[MAX_WATER_RIPPLES] = {-1, -1, -1, -1};
static int s_waterLocRippleParams[MAX_WATER_RIPPLES] = {-1, -1, -1, -1};
static int s_waterLocObstacles[MAX_WATER_OBSTACLES] = {-1, -1, -1, -1};
static int s_waterLocWaveFieldTex = -1;
static int s_waterLocWaveFieldEnabled = -1;

static Shader s_waterBedShader = {0};
static bool s_waterBedShaderReady = false;
static int s_bedLocTime = -1;
static int s_bedLocWaterHeight = -1;
static int s_bedLocLightDir = -1;
static int s_bedLocLightColor = -1;
static int s_bedLocAmbientColor = -1;
static int s_bedLocViewPos = -1;
static int s_bedLocCausticsStrength = -1;
static int s_bedLocCausticsScale = -1;
static int s_bedLocAbsorption = -1;
static int s_bedLocDeepColor = -1;
static int s_bedLocShallowColor = -1;
static int s_bedLocCausticTex = -1;
static int s_bedLocModelPos = -1;

#define NATURE_INTERACTION_RESOLUTION 64
#define NATURE_INTERACTION_PIXEL_COUNT \
    (NATURE_INTERACTION_RESOLUTION * NATURE_INTERACTION_RESOLUTION)
static const float kNatureInteractionWorldSize = 18.0f;
static const float kNatureInteractionMaxBend = 0.55f;
static const float kNatureWindReferenceSpeed = 1.7f;
static const float kNatureWindBendPerMps = 0.035f;
static Texture2D s_natureInteractionTexture = {0};
static Color s_natureInteractionPixels[NATURE_INTERACTION_PIXEL_COUNT];
static Color s_natureInteractionScratch[NATURE_INTERACTION_PIXEL_COUNT];
static Color s_natureWindPixels[NATURE_INTERACTION_PIXEL_COUNT];
static Vector2 s_natureInteractionCenter = {0};
static bool s_natureInteractionReady = false;
static bool s_natureInteractionOpen = false;
static bool s_natureWindReceiverReady = false;
static WindMacroConfig s_natureWindMacro = {0};
static float s_natureInteractionHeight = 0.0f;
static bool s_natureWindImpactEnabled = false;
static Vector2 s_natureWindImpactCenter = {0};
static Vector2 s_natureWindImpactDirection = {1.0f, 0.0f};
static int s_natureWindImpactType = 0;
static float s_natureWindImpactRadius = 0.0f;
static float s_natureWindImpactStrength = 0.0f;
static float s_natureWindImpactAge = 0.0f;
static int s_natureWindTraceDominantSlot = -1;
static bool s_natureWindTraceVisibleUploaded = false;
static bool s_natureWindTraceShadowUploaded = false;
static MapNatureRenderStats s_natureRenderStats = {0};

static bool Nature_WindTraceEnabled(void)
{
    const char *value = getenv("WUXING_WIND_RECEIVER_TRACE");
    return value != NULL && value[0] != '\0' && value[0] != '0';
}

// Under rlvk SetShaderValue targets the currently active shader, not merely
// the Shader argument. Keep every vegetation uniform upload and its draw in
// one explicit scope so custom wind uniforms cannot land on the previous pass.
static void Nature_BeginWindReceiverShader(Shader shader)
{
    BeginShaderMode(shader); // WIND_RECEIVER_UNIFORM_SCOPE
}

static void Nature_EndWindReceiverShader(void)
{
    EndShaderMode(); // WIND_RECEIVER_UNIFORM_SCOPE
}

// Runtime A/B switch for validating the two vegetation-shadow layers without
// rebuilding a map.  The default is the production hybrid path; the explicit
// modes are intentionally diagnostics and are shared by every map using this
// toolkit.
typedef enum NatureShadowMode {
    NATURE_SHADOW_HYBRID = 0,
    NATURE_SHADOW_REAL_ONLY,
    NATURE_SHADOW_PROJECTED_ONLY,
} NatureShadowMode;

static NatureShadowMode Nature_GetShadowMode(void)
{
    const char *mode = getenv("WUXING_NATURE_SHADOW_MODE");
    if (mode != NULL && (mode[0] == 'r' || mode[0] == 'R'))
        return NATURE_SHADOW_REAL_ONLY;
    if (mode != NULL && (mode[0] == 'p' || mode[0] == 'P'))
        return NATURE_SHADOW_PROJECTED_ONLY;
    return NATURE_SHADOW_HYBRID;
}

static bool Nature_ShadowCasterTypeEnabled(bool flower)
{
    const char *filter = getenv("WUXING_NATURE_SHADOW_CASTERS");
    if (filter == NULL || filter[0] == '\0' || filter[0] == 'a' || filter[0] == 'A')
        return true;
    if (filter[0] == 'f' || filter[0] == 'F')
        return flower;
    if (filter[0] == 'm' || filter[0] == 'M' || filter[0] == 'g' || filter[0] == 'G')
        return !flower;
    if (filter[0] == 'n' || filter[0] == 'N')
        return false;
    return true;
}

static bool Nature_ShadowCasterFilterActive(void)
{
    const char *filter = getenv("WUXING_NATURE_SHADOW_CASTERS");
    return filter != NULL && filter[0] != '\0' &&
           filter[0] != 'a' && filter[0] != 'A';
}

void MapProp_ResetNatureRenderStats(void)
{
    s_natureRenderStats = (MapNatureRenderStats){0};
}

MapNatureRenderStats MapProp_GetNatureRenderStats(void)
{
    return s_natureRenderStats;
}

static Color Nature_EmptyInteractionPixel(void)
{
    return (Color){128, 128, 0, 255};
}

static Vector2 Nature_DecodeInteractionPixel(Color pixel)
{
    float bend = (float)pixel.b / 255.0f * kNatureInteractionMaxBend;
    return (Vector2){
        ((float)pixel.r / 255.0f * 2.0f - 1.0f) * bend,
        ((float)pixel.g / 255.0f * 2.0f - 1.0f) * bend,
    };
}

static Color Nature_EncodeInteractionPixel(Vector2 bend)
{
    float magnitude = sqrtf(bend.x * bend.x + bend.y * bend.y);
    if (magnitude <= 0.0001f)
        return Nature_EmptyInteractionPixel();
    if (magnitude > kNatureInteractionMaxBend) {
        float scale = kNatureInteractionMaxBend / magnitude;
        bend.x *= scale;
        bend.y *= scale;
        magnitude = kNatureInteractionMaxBend;
    }
    float inverseMagnitude = 1.0f / magnitude;
    float directionX = fmaxf(-1.0f, fminf(1.0f, bend.x * inverseMagnitude));
    float directionZ = fmaxf(-1.0f, fminf(1.0f, bend.y * inverseMagnitude));
    return (Color){
        (unsigned char)((directionX * 0.5f + 0.5f) * 255.0f),
        (unsigned char)((directionZ * 0.5f + 0.5f) * 255.0f),
        (unsigned char)(magnitude / kNatureInteractionMaxBend * 255.0f),
        255,
    };
}

static void Nature_InitInteraction(void)
{
    if (s_natureInteractionReady)
        return;
    Color empty = Nature_EmptyInteractionPixel();
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = empty;
    Image image = GenImageColor(NATURE_INTERACTION_RESOLUTION,
                                NATURE_INTERACTION_RESOLUTION, empty);
    s_natureInteractionTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(s_natureInteractionTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(s_natureInteractionTexture, TEXTURE_WRAP_CLAMP);
    s_natureInteractionReady = s_natureInteractionTexture.id != 0;
}

static void Nature_ScrollAndDecayInteraction(Vector2 newCenter, float dt)
{
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    int shiftX = (int)roundf((newCenter.x - s_natureInteractionCenter.x) / cellSize);
    int shiftY = (int)roundf((newCenter.y - s_natureInteractionCenter.y) / cellSize);
    float decay = expf(-fmaxf(dt, 0.0f) * 2.15f);
    Color empty = Nature_EmptyInteractionPixel();

    for (int y = 0; y < NATURE_INTERACTION_RESOLUTION; y++) {
        for (int x = 0; x < NATURE_INTERACTION_RESOLUTION; x++) {
            int sourceX = x + shiftX;
            int sourceY = y + shiftY;
            Color value = empty;
            if (sourceX >= 0 && sourceX < NATURE_INTERACTION_RESOLUTION &&
                sourceY >= 0 && sourceY < NATURE_INTERACTION_RESOLUTION) {
                value = s_natureInteractionPixels[sourceY * NATURE_INTERACTION_RESOLUTION + sourceX];
                value.b = (unsigned char)((float)value.b * decay);
                if (value.b < 2)
                    value = empty;
            }
            s_natureInteractionScratch[y * NATURE_INTERACTION_RESOLUTION + x] = value;
        }
    }
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = s_natureInteractionScratch[i];
    s_natureInteractionCenter = newCenter;
}

void MapProp_BeginNatureInteraction(Vector3 focus, float dt)
{
    Nature_InitInteraction();
    if (!s_natureInteractionReady)
        return;
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    Vector2 snappedCenter = {
        roundf(focus.x / cellSize) * cellSize,
        roundf(focus.z / cellSize) * cellSize,
    };
    Nature_ScrollAndDecayInteraction(snappedCenter, dt);
    Color empty = Nature_EmptyInteractionPixel();
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureWindPixels[i] = empty;
    s_natureInteractionHeight = focus.y;
    s_natureWindReceiverReady = false;
    s_natureWindImpactEnabled = false;
    s_natureInteractionOpen = true;
}

void MapProp_AddNatureInteractor(Vector3 position, float radius, float strength)
{
    if (!s_natureInteractionOpen || radius <= 0.0f || strength <= 0.0f)
        return;
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    float halfSize = kNatureInteractionWorldSize * 0.5f;
    int minX = (int)floorf((position.x - radius - (s_natureInteractionCenter.x - halfSize)) / cellSize);
    int maxX = (int)ceilf((position.x + radius - (s_natureInteractionCenter.x - halfSize)) / cellSize);
    int minY = (int)floorf((position.z - radius - (s_natureInteractionCenter.y - halfSize)) / cellSize);
    int maxY = (int)ceilf((position.z + radius - (s_natureInteractionCenter.y - halfSize)) / cellSize);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= NATURE_INTERACTION_RESOLUTION) maxX = NATURE_INTERACTION_RESOLUTION - 1;
    if (maxY >= NATURE_INTERACTION_RESOLUTION) maxY = NATURE_INTERACTION_RESOLUTION - 1;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float worldX = s_natureInteractionCenter.x - halfSize + (x + 0.5f) * cellSize;
            float worldZ = s_natureInteractionCenter.y - halfSize + (y + 0.5f) * cellSize;
            float dx = worldX - position.x;
            float dz = worldZ - position.z;
            float distance = sqrtf(dx * dx + dz * dz);
            if (distance >= radius)
                continue;
            float falloff = 1.0f - distance / radius;
            falloff *= falloff;
            float bend = fminf(strength / kNatureInteractionMaxBend, 1.0f) * falloff;
            Color *pixel = &s_natureInteractionPixels[y * NATURE_INTERACTION_RESOLUTION + x];
            unsigned char encodedBend = (unsigned char)(bend * 255.0f);
            if (encodedBend <= pixel->b)
                continue;
            float inverseDistance = distance > 0.0001f ? 1.0f / distance : 0.0f;
            pixel->r = (unsigned char)((dx * inverseDistance * 0.5f + 0.5f) * 255.0f);
            pixel->g = (unsigned char)((dz * inverseDistance * 0.5f + 0.5f) * 255.0f);
            pixel->b = encodedBend;
            pixel->a = 255;
        }
    }
}

static int Nature_UpdateDominantWindImpact(const VorticleData *vorticles,
                                           int vorticleCount, float time)
{
    int strongestFocus = -1;
    int directImpact = -1;
    float strongestFocusScore = 0.0f;
    float directImpactScore = 0.0f;
    for (int i = 0; i < vorticleCount; i++) {
        const VorticleData *vorticle = &vorticles[i];
        if (!vorticle->active || vorticle->radius <= 0.0f)
            continue;
        float lifetimeWeight = vorticle->maxLifetime > 0.0001f
            ? fmaxf(0.0f, vorticle->lifetime / vorticle->maxLifetime)
            : 1.0f;
        float score = fabsf(vorticle->strength) * lifetimeWeight;
        if (score > strongestFocusScore) {
            strongestFocusScore = score;
            strongestFocus = i;
        }
        bool supportsAnalyticDirection =
            vorticle->type == VORTICLE_LINEAR_GUST ||
            vorticle->type == VORTICLE_RADIAL_BLAST;
        if (supportsAnalyticDirection && score > directImpactScore) {
            directImpactScore = score;
            directImpact = i;
        }
    }
    if (strongestFocus < 0) {
        if (Nature_WindTraceEnabled() && s_natureWindTraceDominantSlot >= 0)
            TraceLog(LOG_INFO, "[WIND_TRACE] vegetation_receiver end");
        s_natureWindTraceDominantSlot = -1;
        s_natureWindTraceVisibleUploaded = false;
        s_natureWindTraceShadowUploaded = false;
        return -1;
    }

    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    const VorticleData *focus = &vorticles[strongestFocus];
    Vector2 windCenter = {
        roundf(focus->position.x / cellSize) * cellSize,
        roundf(focus->position.z / cellSize) * cellSize,
    };
    Nature_ScrollAndDecayInteraction(windCenter, 0.0f);
    s_natureInteractionHeight = focus->position.y;

    // Vortex and turbulence have position-dependent direction fields. They
    // must stay in the rasterized receiver path; collapsing either to one
    // uniform direction makes the entire patch rotate or flip as one sheet.
    if (directImpact < 0) {
        s_natureWindImpactEnabled = false;
        return -1;
    }
    const VorticleData *source = &vorticles[directImpact];

    // Linear and radial sources have cheap analytic directions. Keep their
    // strongest impact on the direct path while all richer fields are still
    // superposed spatially through the interaction map.
    Vector3 directionSample = {
        source->position.x + source->radius * 0.31f,
        source->position.y,
        source->position.z + source->radius * 0.19f,
    };
    Vector3 velocity = Wind_EvaluateVorticleVelocity(source, directionSample, time);
    float speedXZ = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
    if (speedXZ > 0.0001f) {
        s_natureWindImpactDirection = (Vector2){velocity.x / speedXZ,
                                                 velocity.z / speedXZ};
    } else {
        Vector2 fallback = {source->direction.x, source->direction.z};
        float fallbackLength = sqrtf(fallback.x * fallback.x + fallback.y * fallback.y);
        s_natureWindImpactDirection = fallbackLength > 0.0001f
            ? (Vector2){fallback.x / fallbackLength, fallback.y / fallbackLength}
            : (Vector2){1.0f, 0.0f};
    }
    float lifetimeWeight = source->maxLifetime > 0.0001f
        ? fmaxf(0.0f, source->lifetime / source->maxLifetime) : 1.0f;
    float temporalResponse = source->type == VORTICLE_RADIAL_BLAST
        ? sqrtf(lifetimeWeight) : lifetimeWeight;
    float sourceBend = fmaxf(speedXZ,
                             fabsf(source->strength) * temporalResponse * 0.65f)
                       * kNatureWindBendPerMps;
    s_natureWindImpactCenter = (Vector2){source->position.x, source->position.z};
    s_natureWindImpactType = (int)source->type;
    s_natureWindImpactRadius = source->radius;
    s_natureWindImpactStrength = fminf(sourceBend, kNatureInteractionMaxBend);
    float age01 = source->maxLifetime > 0.0001f
        ? 1.0f - source->lifetime / source->maxLifetime : 0.0f;
    s_natureWindImpactAge = fminf(fmaxf(age01, 0.0f), 1.0f);
    s_natureWindImpactEnabled = s_natureWindImpactStrength > 0.0001f;
    if (Nature_WindTraceEnabled() &&
        s_natureWindTraceDominantSlot != directImpact) {
        TraceLog(LOG_INFO,
                 "[WIND_TRACE] vegetation_receiver count=%d focus_slot=%d direct_slot=%d type=%d center=(%.2f,%.2f,%.2f) radius=%.2f bend=%.3f age=%.3f",
                 vorticleCount, strongestFocus, directImpact, (int)source->type,
                 source->position.x, source->position.y, source->position.z,
                 source->radius, s_natureWindImpactStrength,
                 s_natureWindImpactAge);
        s_natureWindTraceVisibleUploaded = false;
        s_natureWindTraceShadowUploaded = false;
    }
    s_natureWindTraceDominantSlot = directImpact;
    return directImpact;
}

void MapProp_AddNatureWindVorticles(float time)
{
    if (!s_natureInteractionOpen)
        return;

    s_natureWindMacro = Wind_GetMacro();
    s_natureWindReceiverReady = true;

    int vorticleCount = 0;
    const VorticleData *vorticles = Wind_GetActiveVorticles(&vorticleCount);
    int directImpact = Nature_UpdateDominantWindImpact(vorticles,
                                                        vorticleCount, time);
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    float halfSize = kNatureInteractionWorldSize * 0.5f;
    float fieldMinX = s_natureInteractionCenter.x - halfSize;
    float fieldMinZ = s_natureInteractionCenter.y - halfSize;

    for (int i = 0; i < vorticleCount; i++) {
        const VorticleData *vorticle = &vorticles[i];
        if (i == directImpact || !vorticle->active || vorticle->radius <= 0.0f)
            continue;
        int minX = (int)floorf((vorticle->position.x - vorticle->radius - fieldMinX) /
                               cellSize);
        int maxX = (int)ceilf((vorticle->position.x + vorticle->radius - fieldMinX) /
                              cellSize);
        int minY = (int)floorf((vorticle->position.z - vorticle->radius - fieldMinZ) /
                               cellSize);
        int maxY = (int)ceilf((vorticle->position.z + vorticle->radius - fieldMinZ) /
                              cellSize);
        if (minX < 0) minX = 0;
        if (minY < 0) minY = 0;
        if (maxX >= NATURE_INTERACTION_RESOLUTION) maxX = NATURE_INTERACTION_RESOLUTION - 1;
        if (maxY >= NATURE_INTERACTION_RESOLUTION) maxY = NATURE_INTERACTION_RESOLUTION - 1;
        if (minX > maxX || minY > maxY)
            continue;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                Vector3 samplePosition = {
                    fieldMinX + ((float)x + 0.5f) * cellSize,
                    s_natureInteractionHeight,
                    fieldMinZ + ((float)y + 0.5f) * cellSize,
                };
                Vector3 airVelocity = Wind_EvaluateVorticleVelocity(vorticle,
                                                                     samplePosition,
                                                                     time);
                float speedXZ = sqrtf(airVelocity.x * airVelocity.x +
                                      airVelocity.z * airVelocity.z);
                if (speedXZ <= 0.0001f)
                    continue;
                Color *pixel = &s_natureWindPixels[
                    y * NATURE_INTERACTION_RESOLUTION + x];
                Vector2 existingBend = Nature_DecodeInteractionPixel(*pixel);
                Vector2 combinedBend = {
                    existingBend.x + airVelocity.x * kNatureWindBendPerMps,
                    existingBend.y + airVelocity.z * kNatureWindBendPerMps,
                };
                *pixel = Nature_EncodeInteractionPixel(combinedBend);
            }
        }
    }
}

void MapProp_EndNatureInteraction(void)
{
    if (!s_natureInteractionOpen || !s_natureInteractionReady)
        return;
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++) {
        Vector2 interactionBend = Nature_DecodeInteractionPixel(s_natureInteractionPixels[i]);
        Vector2 windBend = Nature_DecodeInteractionPixel(s_natureWindPixels[i]);
        s_natureInteractionScratch[i] = Nature_EncodeInteractionPixel((Vector2){
            interactionBend.x + windBend.x,
            interactionBend.y + windBend.y,
        });
    }
    UpdateTexture(s_natureInteractionTexture, s_natureInteractionScratch);
    s_natureInteractionOpen = false;
}

void MapProp_ClearNatureInteraction(void)
{
    Color empty = Nature_EmptyInteractionPixel();
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++) {
        s_natureInteractionPixels[i] = empty;
        s_natureInteractionScratch[i] = empty;
        s_natureWindPixels[i] = empty;
    }
    if (s_natureInteractionReady)
        UnloadTexture(s_natureInteractionTexture);
    s_natureInteractionTexture = (Texture2D){0};
    s_natureInteractionCenter = (Vector2){0};
    s_natureInteractionReady = false;
    s_natureInteractionOpen = false;
    s_natureWindReceiverReady = false;
    s_natureWindMacro = (WindMacroConfig){0};
    s_natureInteractionHeight = 0.0f;
    s_natureWindImpactEnabled = false;
    s_natureWindImpactCenter = (Vector2){0};
    s_natureWindImpactDirection = (Vector2){1.0f, 0.0f};
    s_natureWindImpactType = 0;
    s_natureWindImpactRadius = 0.0f;
    s_natureWindImpactStrength = 0.0f;
    s_natureWindImpactAge = 0.0f;
    s_natureWindTraceDominantSlot = -1;
    s_natureWindTraceVisibleUploaded = false;
    s_natureWindTraceShadowUploaded = false;
}

static Color Nature_LerpColor(Color a, Color b, float t)
{
    Color result = {
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255
    };
    return result;
}

static Color Nature_ScaleColor(Color color, float scale)
{
    int r = (int)(color.r * scale);
    int g = (int)(color.g * scale);
    int b = (int)(color.b * scale);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

static Shader Nature_GetShader(bool cutout)
{
    Shader *shader = cutout ? &s_natureCutoutShader : &s_natureOpaqueShader;
    bool *ready = cutout ? &s_natureCutoutShaderReady : &s_natureOpaqueShaderReady;
    if (!*ready) {
        *shader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/nature_lit.vs",
            cutout ? "maps/toolkit/shaders/nature_lit.fs"
                   : "maps/toolkit/shaders/nature_opaque.fs");
        shader->locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(*shader, "vertexPosition");
        shader->locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(*shader, "vertexTexCoord");
        shader->locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(*shader, "vertexTexCoord2");
        shader->locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(*shader, "vertexNormal");
        shader->locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(*shader, "vertexColor");
        shader->locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(*shader, "mvp");
        shader->locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(*shader, "matModel");
        shader->locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(*shader, "colDiffuse");
        shader->locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(*shader, "texture0");
        MapShadow_ConfigureShader(*shader);
        VFXLight_RegisterShader(*shader);
        *ready = true;
    }
    return *shader;
}

static Shader NatureShadow_GetShader(void)
{
    if (!s_natureShadowShaderReady) {
        s_natureShadowShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/nature_shadow.vs",
            "maps/toolkit/shaders/nature_shadow.fs");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_POSITION] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexPosition");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexTexCoord");
        s_natureShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD02] =
            GetShaderLocationAttrib(s_natureShadowShader, "vertexTexCoord2");
        s_natureShadowShader.locs[SHADER_LOC_MATRIX_MVP] =
            GetShaderLocation(s_natureShadowShader, "mvp");
        s_natureShadowShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(s_natureShadowShader, "matModel");
        s_natureShadowShader.locs[SHADER_LOC_MAP_DIFFUSE] =
            GetShaderLocation(s_natureShadowShader, "texture0");
        s_natureShadowShaderReady = true;
    }
    return s_natureShadowShader;
}

static Shader Water_GetShader(void)
{
    if (!s_waterShaderReady) {
        s_waterShader = ResourceManager_LoadShader("maps/toolkit/shaders/water_surface.vs",
                                                   "maps/toolkit/shaders/water_surface.fs");
        s_waterShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s_waterShader, "vertexPosition");
        s_waterShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s_waterShader, "vertexTexCoord");
        s_waterShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s_waterShader, "mvp");
        s_waterShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s_waterShader, "matModel");
        s_waterShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_waterShader, "colDiffuse");
        s_waterShader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(s_waterShader, "texture0");

        s_waterLocTime = GetShaderLocation(s_waterShader, "u_time");
        s_waterLocWaveHeight = GetShaderLocation(s_waterShader, "u_waveHeight");
        s_waterLocWaveScale = GetShaderLocation(s_waterShader, "u_waveScale");
        s_waterLocWaveSpeed = GetShaderLocation(s_waterShader, "u_waveSpeed");
        s_waterLocDetailScale = GetShaderLocation(s_waterShader, "u_detailScale");
        s_waterLocDetailStrength = GetShaderLocation(s_waterShader, "u_detailStrength");
        s_waterLocFlowVelocity = GetShaderLocation(s_waterShader, "u_flowVelocity");
        s_waterLocWaterShape = GetShaderLocation(s_waterShader, "u_waterShape");
        s_waterLocLightDir = GetShaderLocation(s_waterShader, "u_lightDir");
        s_waterLocLightColor = GetShaderLocation(s_waterShader, "u_lightColor");
        s_waterLocAmbientColor = GetShaderLocation(s_waterShader, "u_ambientColor");
        s_waterLocViewPos = GetShaderLocation(s_waterShader, "u_viewPos");
        s_waterLocMaxDepth = GetShaderLocation(s_waterShader, "u_maxDepth");
        s_waterLocAbsorption = GetShaderLocation(s_waterShader, "u_absorption");
        s_waterLocDeepColor = GetShaderLocation(s_waterShader, "u_deepColor");
        s_waterLocShallowColor = GetShaderLocation(s_waterShader, "u_shallowColor");
        s_waterLocFoamColor = GetShaderLocation(s_waterShader, "u_foamColor");
        s_waterLocScatterColor = GetShaderLocation(s_waterShader, "u_scatterColor");
        s_waterLocScatterCoeff = GetShaderLocation(s_waterShader, "u_scatterCoeff");
        s_waterLocCausticsStrength = GetShaderLocation(s_waterShader, "u_causticsStrength");
        s_waterLocCausticsScale = GetShaderLocation(s_waterShader, "u_causticsScale");
        s_waterLocFoamThreshold = GetShaderLocation(s_waterShader, "u_foamThreshold");
        s_waterLocCausticTex = GetShaderLocation(s_waterShader, "u_causticTex");
        s_waterLocCameraDepthTex = GetShaderLocation(s_waterShader, "u_cameraDepthTex");
        s_waterLocHasDepthTex = GetShaderLocation(s_waterShader, "u_hasDepthTex");
        s_waterLocModelPos = GetShaderLocation(s_waterShader, "u_modelPos");
        s_waterLocResolution = GetShaderLocation(s_waterShader, "u_resolution");
        s_waterLocInteractor = GetShaderLocation(s_waterShader, "u_waterInteractor");
        s_waterLocVelocity = GetShaderLocation(s_waterShader, "u_waterVelocity");
        s_waterLocRadius = GetShaderLocation(s_waterShader, "u_waterRadius");
        static const char *ringNames[] = {"u_rippleRing0", "u_rippleRing1", "u_rippleRing2", "u_rippleRing3"};
        static const char *paramNames[] = {"u_rippleParam0", "u_rippleParam1", "u_rippleParam2", "u_rippleParam3"};
        static const char *obstacleNames[] = {"u_obstacle0", "u_obstacle1", "u_obstacle2", "u_obstacle3"};
        for (int i = 0; i < MAX_WATER_RIPPLES; i++) {
            s_waterLocRippleRings[i] = GetShaderLocation(s_waterShader, ringNames[i]);
            s_waterLocRippleParams[i] = GetShaderLocation(s_waterShader, paramNames[i]);
        }
        for (int i = 0; i < MAX_WATER_OBSTACLES; i++)
            s_waterLocObstacles[i] = GetShaderLocation(s_waterShader, obstacleNames[i]);
        s_waterLocWaveFieldTex = GetShaderLocation(s_waterShader, "u_waveFieldTex");
        s_waterLocWaveFieldEnabled = GetShaderLocation(s_waterShader, "u_waveFieldEnabled");

        int causticSlot = 1;
        if (s_waterLocCausticTex >= 0) {
            SetShaderValue(s_waterShader, s_waterLocCausticTex, &causticSlot, SHADER_UNIFORM_INT);
        }
        int depthSlot = 2;
        if (s_waterLocCameraDepthTex >= 0) {
            SetShaderValue(s_waterShader, s_waterLocCameraDepthTex, &depthSlot, SHADER_UNIFORM_INT);
        }
        int waveSlot = 3;
        if (s_waterLocWaveFieldTex >= 0)
            SetShaderValue(s_waterShader, s_waterLocWaveFieldTex, &waveSlot, SHADER_UNIFORM_INT);

        VFXLight_RegisterShader(s_waterShader);
        s_waterShaderReady = true;
    }
    return s_waterShader;
}

static Shader Water_GetBedShader(void)
{
    if (!s_waterBedShaderReady) {
        s_waterBedShader = ResourceManager_LoadShader("maps/toolkit/shaders/water_bed.vs",
                                                      "maps/toolkit/shaders/water_bed.fs");
        s_waterBedShader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s_waterBedShader, "vertexPosition");
        s_waterBedShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s_waterBedShader, "vertexTexCoord");
        s_waterBedShader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(s_waterBedShader, "vertexNormal");
        s_waterBedShader.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(s_waterBedShader, "vertexColor");
        s_waterBedShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s_waterBedShader, "mvp");
        s_waterBedShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s_waterBedShader, "matModel");
        s_waterBedShader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_waterBedShader, "colDiffuse");
        s_waterBedShader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(s_waterBedShader, "texture0");

        s_bedLocTime = GetShaderLocation(s_waterBedShader, "u_time");
        s_bedLocWaterHeight = GetShaderLocation(s_waterBedShader, "u_waterHeight");
        s_bedLocLightDir = GetShaderLocation(s_waterBedShader, "u_lightDir");
        s_bedLocLightColor = GetShaderLocation(s_waterBedShader, "u_lightColor");
        s_bedLocAmbientColor = GetShaderLocation(s_waterBedShader, "u_ambientColor");
        s_bedLocViewPos = GetShaderLocation(s_waterBedShader, "u_viewPos");
        s_bedLocCausticsStrength = GetShaderLocation(s_waterBedShader, "u_causticsStrength");
        s_bedLocCausticsScale = GetShaderLocation(s_waterBedShader, "u_causticsScale");
        s_bedLocAbsorption = GetShaderLocation(s_waterBedShader, "u_absorption");
        s_bedLocDeepColor = GetShaderLocation(s_waterBedShader, "u_deepColor");
        s_bedLocShallowColor = GetShaderLocation(s_waterBedShader, "u_shallowColor");
        s_bedLocCausticTex = GetShaderLocation(s_waterBedShader, "u_causticTex");
        s_bedLocModelPos = GetShaderLocation(s_waterBedShader, "u_modelPos");

        int causticSlot = 1;
        if (s_bedLocCausticTex >= 0) {
            SetShaderValue(s_waterBedShader, s_bedLocCausticTex, &causticSlot, SHADER_UNIFORM_INT);
        }
        s_waterBedShaderReady = true;
    }
    return s_waterBedShader;
}

static Shader FlowerShadow_GetShader(void)
{
    if (!s_flowerShadowShaderReady) {
        s_flowerShadowShader = ResourceManager_LoadShader(
            "maps/toolkit/shaders/flower_shadow.vs",
            "maps/toolkit/shaders/flower_shadow.fs");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_POSITION] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexPosition");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexTexCoord");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_NORMAL] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexNormal");
        s_flowerShadowShader.locs[SHADER_LOC_VERTEX_COLOR] =
            GetShaderLocationAttrib(s_flowerShadowShader, "vertexColor");
        s_flowerShadowShader.locs[SHADER_LOC_MATRIX_MVP] =
            GetShaderLocation(s_flowerShadowShader, "mvp");
        s_flowerShadowShader.locs[SHADER_LOC_MATRIX_MODEL] =
            GetShaderLocation(s_flowerShadowShader, "matModel");
        s_flowerShadowShaderReady = true;
    }
    return s_flowerShadowShader;
}

static void Nature_UpdateProjectedShadowShader(Shader shader, bool realShadowActive)
{
    Vector3 lightTravel = Environment_GetSunDirection();
    Vector4 shadowColor = ColorNormalize(Environment_GetShadowColor());
    Vector3 shadowTint = {shadowColor.x, shadowColor.y, shadowColor.z};
    // With a real directional map this mesh is a short grounding/contact layer.
    // Keep it visibly shorter than the true animated silhouette, but broad and
    // rounded enough to survive a high gameplay camera and the grass texture.
    // MED and SHADOW OFF retain the full inexpensive fallback.
    // Hybrid contact is root occlusion, not a second directional silhouette.
    // Keep it short, broad and restrained so the real animated shadow owns the
    // readable shape. SHADOW OFF retains a softer projected fallback.
    float projectionScale = realShadowActive ? 0.42f : 0.68f;
    float widthScale = realShadowActive ? 1.50f : 1.25f;
    float tipWidth = realShadowActive ? 0.92f : 0.75f;
    float shadowStrength = realShadowActive ? 0.85f : 0.92f;
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightTravel"),
                   &lightTravel, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_shadowTint"),
                   &shadowTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_projectionScale"),
                   &projectionScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_widthScale"),
                   &widthScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_tipWidth"),
                   &tipWidth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_shadowStrength"),
                   &shadowStrength, SHADER_UNIFORM_FLOAT);
}

static void Nature_SetVertex(Mesh *mesh, int index, Vector3 p, Vector3 n,
                             float phase, float heightMask, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->normals[index * 3 + 0] = n.x;
    mesh->normals[index * 3 + 1] = n.y;
    mesh->normals[index * 3 + 2] = n.z;
    mesh->texcoords[index * 2 + 0] = phase;
    mesh->texcoords[index * 2 + 1] = heightMask;
    mesh->texcoords2[index * 2 + 0] = -1.0f;
    mesh->texcoords2[index * 2 + 1] = heightMask;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

static void Nature_SetUv2(Mesh *mesh, int index, float u, float v)
{
    mesh->texcoords2[index * 2 + 0] = u;
    mesh->texcoords2[index * 2 + 1] = v;
}

static void Nature_AddQuad(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                           Vector3 p2, Vector3 p3, Vector3 normal,
                           float phase, float h0, float h1, Color c0, Color c1)
{
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p3, normal, phase, h1, c1);
}

static void Nature_AddTexturedQuad(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                   Vector3 p2, Vector3 p3, Vector3 normal,
                                   float phase, float h0, float h1, Color c0, Color c1)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal, phase, h0, h1, c0, c1);
    Nature_SetUv2(mesh, base + 0, 0.0f, h0);
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 1.0f, h1);
    Nature_SetUv2(mesh, base + 3, 0.0f, h0);
    Nature_SetUv2(mesh, base + 4, 1.0f, h1);
    Nature_SetUv2(mesh, base + 5, 0.0f, h1);
}

static void Nature_AddTexturedBloom(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                    Vector3 p2, Vector3 p3, Vector3 normal,
                                    float phase, Color color, Vector4 uvRect)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal,
                   phase, 1.0f, 1.0f, color, color);
    Nature_SetUv2(mesh, base + 0, uvRect.x, uvRect.y);
    Nature_SetUv2(mesh, base + 1, uvRect.z, uvRect.y);
    Nature_SetUv2(mesh, base + 2, uvRect.z, uvRect.w);
    Nature_SetUv2(mesh, base + 3, uvRect.x, uvRect.y);
    Nature_SetUv2(mesh, base + 4, uvRect.z, uvRect.w);
    Nature_SetUv2(mesh, base + 5, uvRect.x, uvRect.w);
}

static void Nature_AddTexturedQuadUV(Mesh *mesh, int *cursor,
                                     Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                                     Vector3 normal, float phase, float h0, float h1,
                                     Color c0, Color c1,
                                     float u0, float v0, float u1, float v1)
{
    int base = *cursor;
    Nature_AddQuad(mesh, cursor, p0, p1, p2, p3, normal, phase, h0, h1, c0, c1);
    Nature_SetUv2(mesh, base + 0, u0, v1);
    Nature_SetUv2(mesh, base + 1, u1, v1);
    Nature_SetUv2(mesh, base + 2, u1, v0);
    Nature_SetUv2(mesh, base + 3, u0, v1);
    Nature_SetUv2(mesh, base + 4, u1, v0);
    Nature_SetUv2(mesh, base + 5, u0, v0);
}

static void Nature_AddPointedBladeTip(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                      Vector3 tip, Vector3 normal, float phase,
                                      float h0, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, tip, normal, phase, 1.0f, c1);
    Nature_SetUv2(mesh, base + 0, 0.0f, h0);
    Nature_SetUv2(mesh, base + 1, 1.0f, h0);
    Nature_SetUv2(mesh, base + 2, 0.5f, 1.0f);
}

static void Nature_AddCurvedBladeQuad(Mesh *mesh, int *cursor,
                                      Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                                      Vector3 n0, Vector3 n1, Vector3 n2, Vector3 n3,
                                      float phase, float h0, float h1, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, n1, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, n2, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, n2, phase, h1, c1);
    Nature_SetVertex(mesh, (*cursor)++, p3, n3, phase, h1, c1);
    // Ghost of Tsushima: transverse coordinate u in [-1.0, +1.0] across blade width
    Nature_SetUv2(mesh, base + 0, -1.0f, h0);
    Nature_SetUv2(mesh, base + 1,  1.0f, h0);
    Nature_SetUv2(mesh, base + 2,  1.0f, h1);
    Nature_SetUv2(mesh, base + 3, -1.0f, h0);
    Nature_SetUv2(mesh, base + 4,  1.0f, h1);
    Nature_SetUv2(mesh, base + 5, -1.0f, h1);
}

static void Nature_AddCurvedBladeTip(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                                     Vector3 tip, Vector3 n0, Vector3 n1, Vector3 nTip,
                                     float phase, float h0, Color c0, Color c1)
{
    int base = *cursor;
    Nature_SetVertex(mesh, (*cursor)++, p0, n0, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, n1, phase, h0, c0);
    Nature_SetVertex(mesh, (*cursor)++, tip, nTip, phase, 1.0f, c1);
    Nature_SetUv2(mesh, base + 0, -1.0f, h0);
    Nature_SetUv2(mesh, base + 1,  1.0f, h0);
    Nature_SetUv2(mesh, base + 2,  0.0f, 1.0f); // Tip apex is center spine (u = 0.0)
}

static void Nature_AddQuad4(Mesh *mesh, int *cursor, Vector3 p0, Vector3 p1,
                            Vector3 p2, Vector3 p3, Vector3 normal,
                            float phase, Color c0, Color c1, Color c2, Color c3)
{
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, 0.0f, c0);
    Nature_SetVertex(mesh, (*cursor)++, p1, normal, phase, 0.0f, c1);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, 0.0f, c2);
    Nature_SetVertex(mesh, (*cursor)++, p0, normal, phase, 0.0f, c0);
    Nature_SetVertex(mesh, (*cursor)++, p2, normal, phase, 0.0f, c2);
    Nature_SetVertex(mesh, (*cursor)++, p3, normal, phase, 0.0f, c3);
}

static Mesh Nature_AllocMesh(int vertexCount)
{
    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = vertexCount / 3;
    mesh.vertices = MemAlloc((unsigned int)vertexCount * 3u * sizeof(float));
    mesh.normals = MemAlloc((unsigned int)vertexCount * 3u * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)vertexCount * 2u * sizeof(float));
    mesh.texcoords2 = MemAlloc((unsigned int)vertexCount * 2u * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)vertexCount * 4u * sizeof(unsigned char));
    return mesh;
}

static Model Nature_ModelFromMesh(Mesh mesh, Shader shader);

static Model Nature_BuildFlowerShadowModel(const MapFlowerPlacement *placements, int count)
{
    Mesh mesh = Nature_AllocMesh(count * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float width = fmaxf(flower->bloomRadius * 0.95f, 0.045f);
        Vector3 root = flower->position;
        root.y += 0.0015f;
        Vector3 encoded = {flower->height, width, flower->phase};
        Color rootShade = {180, 180, 180, 255};
        Color tipShade = {110, 110, 110, 255};
        const Vector2 uv[6] = {
            {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
            {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f},
        };
        for (int vertex = 0; vertex < 6; vertex++) {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, flower->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

static Model Nature_BuildMeadowShadowChunk(const MapMeadowPlacement *placements, int count,
                                           float minX, float maxX, float minZ, float maxZ)
{
    int selected = 0;
    for (int i = 0; i < count; i++) {
        Vector3 p = placements[i].position;
        if (p.x >= minX && p.x < maxX && p.z >= minZ && p.z < maxZ)
            selected++;
    }
    if (selected <= 0) return (Model){0};

    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    const Vector2 uv[6] = {
        {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f},
        {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f},
    };
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        Vector3 root = clump->position;
        root.y += 0.0012f;
        float width = fmaxf(clump->radius * 0.72f, 0.038f);
        Vector3 encoded = {clump->height * 1.05f, width, clump->phase};
        Color rootShade = {240, 240, 240, 255};
        Color tipShade = {170, 170, 170, 255};
        for (int vertex = 0; vertex < 6; vertex++) {
            Color shade = vertex < 2 || vertex == 3 ? rootShade : tipShade;
            Nature_SetVertex(&mesh, cursor, root, encoded, clump->phase, 0.0f, shade);
            mesh.texcoords[cursor * 2 + 0] = uv[vertex].x;
            mesh.texcoords[cursor * 2 + 1] = uv[vertex].y;
            cursor++;
        }
    }
    return Nature_ModelFromMesh(mesh, FlowerShadow_GetShader());
}

static Model Nature_ModelFromMesh(Mesh mesh, Shader shader)
{
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].shader = shader;
    MapShadow_AttachMaterial(&model.materials[0]);
    return model;
}

typedef enum NatureWindResponseKind {
    NATURE_WIND_RESPONSE_GRASS = 0,
    NATURE_WIND_RESPONSE_FLOWER,
    NATURE_WIND_RESPONSE_STATIC,
} NatureWindResponseKind;

static Vector4 Nature_GetWindResponse(NatureWindResponseKind kind)
{
    if (kind == NATURE_WIND_RESPONSE_FLOWER)
        return (Vector4){0.12f, 0.08f, 3.2f, 0.72f};
    if (kind == NATURE_WIND_RESPONSE_STATIC)
        return (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
    return (Vector4){0.035f, 0.14f, 6.8f, 1.0f};
}

static void Nature_UpdateWindFieldShader(Shader shader, Vector2 fallbackDirection,
                                         NatureWindResponseKind responseKind)
{
    WindMacroConfig macro = s_natureWindMacro;
    if (!s_natureWindReceiverReady) {
        float directionLength = sqrtf(fallbackDirection.x * fallbackDirection.x +
                                      fallbackDirection.y * fallbackDirection.y);
        Vector2 direction = directionLength > 0.0001f
            ? (Vector2){fallbackDirection.x / directionLength,
                        fallbackDirection.y / directionLength}
            : (Vector2){0.0f, 0.0f};
        macro = (WindMacroConfig){
            .baseDirection = {direction.x * kNatureWindReferenceSpeed, 0.0f,
                              direction.y * kNatureWindReferenceSpeed},
            .gustAmplitude = 0.6f,
            .noiseScale = 0.06f,
            .noiseSpeed = 1.0f,
            .terrainLiftK = 0.0f,
        };
    }
    Vector4 response = Nature_GetWindResponse(responseKind);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windBaseVelocity"),
                   &macro.baseDirection, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windGustAmplitude"),
                   &macro.gustAmplitude, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windNoiseScale"),
                   &macro.noiseScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windNoiseSpeed"),
                   &macro.noiseSpeed, SHADER_UNIFORM_FLOAT);
    int fieldDetail = GfxQuality_Get() >= GFX_MED ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_windFieldDetail"),
                   &fieldDetail, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_natureWindResponse"),
                   &response, SHADER_UNIFORM_VEC4);
}

static void Nature_UpdateShader(Shader shader, float time, Vector2 windDirection, float windStrength,
                                bool useTexture, float alphaCutoff,
                                NatureWindResponseKind responseKind)
{
    float windLength = sqrtf(windDirection.x * windDirection.x + windDirection.y * windDirection.y);
    if (windLength > 0.0001f) {
        windDirection.x /= windLength;
        windDirection.y /= windLength;
    }
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector3 sunRgb = {sun.x, sun.y, sun.z};
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());
    int worldFromShaderSpaceLoc = GetShaderLocation(shader, "u_worldFromShaderSpace");
    if (worldFromShaderSpaceLoc >= 0)
        SetShaderValueMatrix(shader, worldFromShaderSpaceLoc, worldFromShaderSpace);
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windStrength"), &windStrength, SHADER_UNIFORM_FLOAT);
    Nature_UpdateWindFieldShader(shader, windDirection, responseKind);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_lightColor"), &sunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_ambientColor"), &ambientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, GetShaderLocation(shader, "u_viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
    int textured = useTexture ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_useTexture"), &textured, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCutoff"), &alphaCutoff, SHADER_UNIFORM_FLOAT);
    float tipSoftening = 0.0f;
    int tipSofteningLoc = GetShaderLocation(shader, "u_grassTipSoftening");
    if (tipSofteningLoc >= 0)
        SetShaderValue(shader, tipSofteningLoc, &tipSoftening, SHADER_UNIFORM_FLOAT);
    int interactionEnabled = s_natureInteractionReady ? 1 : 0;
    Vector2 interactionCenter = s_natureInteractionCenter;
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionEnabled"),
                   &interactionEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionCenter"),
                   &interactionCenter, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionWorldSize"),
                   &kNatureInteractionWorldSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionMaxBend"),
                   &kNatureInteractionMaxBend, SHADER_UNIFORM_FLOAT);
    int windImpactEnabled = s_natureWindImpactEnabled ? 1 : 0;
    int impactEnabledLoc = GetShaderLocation(shader, "u_windImpactEnabled");
    int impactCenterLoc = GetShaderLocation(shader, "u_windImpactCenter");
    int impactDirectionLoc = GetShaderLocation(shader, "u_windImpactDirection");
    int impactTypeLoc = GetShaderLocation(shader, "u_windImpactType");
    int impactRadiusLoc = GetShaderLocation(shader, "u_windImpactRadius");
    int impactStrengthLoc = GetShaderLocation(shader, "u_windImpactStrength");
    int impactAgeLoc = GetShaderLocation(shader, "u_windImpactAge");
    if (impactEnabledLoc >= 0)
        SetShaderValue(shader, impactEnabledLoc, &windImpactEnabled, SHADER_UNIFORM_INT);
    if (impactCenterLoc >= 0)
        SetShaderValue(shader, impactCenterLoc, &s_natureWindImpactCenter, SHADER_UNIFORM_VEC2);
    if (impactDirectionLoc >= 0)
        SetShaderValue(shader, impactDirectionLoc, &s_natureWindImpactDirection, SHADER_UNIFORM_VEC2);
    if (impactTypeLoc >= 0)
        SetShaderValue(shader, impactTypeLoc, &s_natureWindImpactType, SHADER_UNIFORM_INT);
    if (impactRadiusLoc >= 0)
        SetShaderValue(shader, impactRadiusLoc, &s_natureWindImpactRadius, SHADER_UNIFORM_FLOAT);
    if (impactStrengthLoc >= 0)
        SetShaderValue(shader, impactStrengthLoc, &s_natureWindImpactStrength, SHADER_UNIFORM_FLOAT);
    if (impactAgeLoc >= 0)
        SetShaderValue(shader, impactAgeLoc, &s_natureWindImpactAge, SHADER_UNIFORM_FLOAT);
    if (Nature_WindTraceEnabled() && windImpactEnabled != 0 &&
        !s_natureWindTraceVisibleUploaded) {
        bool uniformsValid = worldFromShaderSpaceLoc >= 0 &&
                             impactEnabledLoc >= 0 && impactCenterLoc >= 0 &&
                             impactDirectionLoc >= 0 && impactTypeLoc >= 0 &&
                             impactRadiusLoc >= 0 &&
                             impactStrengthLoc >= 0 && impactAgeLoc >= 0;
        TraceLog(uniformsValid ? LOG_INFO : LOG_WARNING,
                 "[WIND_TRACE] vegetation_shader pass=visible shader=%u uniforms=%s enabled=%d type=%d center=(%.2f,%.2f) radius=%.2f bend=%.3f age=%.3f",
                 shader.id, uniformsValid ? "ok" : "MISSING", windImpactEnabled,
                 s_natureWindImpactType,
                 s_natureWindImpactCenter.x, s_natureWindImpactCenter.y,
                 s_natureWindImpactRadius, s_natureWindImpactStrength,
                 s_natureWindImpactAge);
        s_natureWindTraceVisibleUploaded = true;
    }
    if (s_natureInteractionReady)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_interactionMap"),
                              s_natureInteractionTexture);
    MapShadow_UpdateShader(shader);
    // Nature's vertex shader converts fragPosition to true world space before
    // projecting it. MapShadow_UpdateShader folds inverse(view) into these
    // matrices for other map shaders whose varyings stay in shader/view space.
    // That conversion here sampled the shadow map at the wrong coordinates.
    if (EnvShadow_IsEnabled()) {
        int lightVpLoc = GetShaderLocation(shader, "u_lightVP");
        if (lightVpLoc >= 0)
            SetShaderValueMatrix(shader, lightVpLoc, EnvShadow_GetLightVP());
        if (EnvShadow_HasStaticCache()) {
            int staticLightVpLoc = GetShaderLocation(shader, "u_staticLightVP");
            if (staticLightVpLoc >= 0)
                SetShaderValueMatrix(shader, staticLightVpLoc,
                                     EnvShadow_GetStaticLightVP());
        }
    }
}

static void Nature_UpdateShadowShader(Shader shader, float time, Vector2 windDirection,
                                      float windStrength, bool useTexture, float alphaCutoff,
                                      NatureWindResponseKind responseKind)
{
    float windLength = sqrtf(windDirection.x * windDirection.x + windDirection.y * windDirection.y);
    if (windLength > 0.0001f) {
        windDirection.x /= windLength;
        windDirection.y /= windLength;
    }
    Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());
    int worldFromShaderSpaceLoc = GetShaderLocation(shader, "u_worldFromShaderSpace");
    if (worldFromShaderSpaceLoc >= 0)
        SetShaderValueMatrix(shader, worldFromShaderSpaceLoc, worldFromShaderSpace);
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"),
                   &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windStrength"),
                   &windStrength, SHADER_UNIFORM_FLOAT);
    Nature_UpdateWindFieldShader(shader, windDirection, responseKind);
    int textured = useTexture ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_useTexture"),
                   &textured, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCutoff"),
                   &alphaCutoff, SHADER_UNIFORM_FLOAT);
    // Conservative alpha coverage is applied only in the shadow capture. It
    // keeps sub-pixel petal tips represented without changing the visible mesh
    // or replacing its authored atlas silhouette with a blob.
    float alphaCoverage = useTexture ? 0.75f : 0.0f;
    SetShaderValue(shader, GetShaderLocation(shader, "u_alphaCoverage"),
                   &alphaCoverage, SHADER_UNIFORM_FLOAT);
    int interactionEnabled = s_natureInteractionReady ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionEnabled"),
                   &interactionEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionCenter"),
                   &s_natureInteractionCenter, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionWorldSize"),
                   &kNatureInteractionWorldSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_interactionMaxBend"),
                   &kNatureInteractionMaxBend, SHADER_UNIFORM_FLOAT);
    int windImpactEnabled = s_natureWindImpactEnabled ? 1 : 0;
    int impactEnabledLoc = GetShaderLocation(shader, "u_windImpactEnabled");
    int impactCenterLoc = GetShaderLocation(shader, "u_windImpactCenter");
    int impactDirectionLoc = GetShaderLocation(shader, "u_windImpactDirection");
    int impactTypeLoc = GetShaderLocation(shader, "u_windImpactType");
    int impactRadiusLoc = GetShaderLocation(shader, "u_windImpactRadius");
    int impactStrengthLoc = GetShaderLocation(shader, "u_windImpactStrength");
    int impactAgeLoc = GetShaderLocation(shader, "u_windImpactAge");
    if (impactEnabledLoc >= 0)
        SetShaderValue(shader, impactEnabledLoc, &windImpactEnabled, SHADER_UNIFORM_INT);
    if (impactCenterLoc >= 0)
        SetShaderValue(shader, impactCenterLoc, &s_natureWindImpactCenter, SHADER_UNIFORM_VEC2);
    if (impactDirectionLoc >= 0)
        SetShaderValue(shader, impactDirectionLoc, &s_natureWindImpactDirection, SHADER_UNIFORM_VEC2);
    if (impactTypeLoc >= 0)
        SetShaderValue(shader, impactTypeLoc, &s_natureWindImpactType, SHADER_UNIFORM_INT);
    if (impactRadiusLoc >= 0)
        SetShaderValue(shader, impactRadiusLoc, &s_natureWindImpactRadius, SHADER_UNIFORM_FLOAT);
    if (impactStrengthLoc >= 0)
        SetShaderValue(shader, impactStrengthLoc, &s_natureWindImpactStrength, SHADER_UNIFORM_FLOAT);
    if (impactAgeLoc >= 0)
        SetShaderValue(shader, impactAgeLoc, &s_natureWindImpactAge, SHADER_UNIFORM_FLOAT);
    if (Nature_WindTraceEnabled() && windImpactEnabled != 0 &&
        !s_natureWindTraceShadowUploaded) {
        bool uniformsValid = worldFromShaderSpaceLoc >= 0 &&
                             impactEnabledLoc >= 0 && impactCenterLoc >= 0 &&
                             impactDirectionLoc >= 0 && impactTypeLoc >= 0 &&
                             impactRadiusLoc >= 0 &&
                             impactStrengthLoc >= 0 && impactAgeLoc >= 0;
        TraceLog(uniformsValid ? LOG_INFO : LOG_WARNING,
                 "[WIND_TRACE] vegetation_shader pass=shadow shader=%u uniforms=%s enabled=%d type=%d center=(%.2f,%.2f) radius=%.2f bend=%.3f age=%.3f",
                 shader.id, uniformsValid ? "ok" : "MISSING", windImpactEnabled,
                 s_natureWindImpactType,
                 s_natureWindImpactCenter.x, s_natureWindImpactCenter.y,
                 s_natureWindImpactRadius, s_natureWindImpactStrength,
                 s_natureWindImpactAge);
        s_natureWindTraceShadowUploaded = true;
    }
    if (s_natureInteractionReady)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_interactionMap"),
                              s_natureInteractionTexture);
}

static unsigned int Nature_NextRandom(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float Nature_Random01(unsigned int *state)
{
    return (float)(Nature_NextRandom(state) & 0x00ffffffu) / 16777215.0f;
}

int MapProp_GenerateMeadowPlacements(MapMeadowPlacement *outPlacements, int maxCount,
                                     const MapGroundSurface *ground, Vector3 groundCenter,
                                     MapMeadowDistribution distribution,
                                     MapFoliageDensityFn densityFn, void *userData)
{
    if (!outPlacements || maxCount <= 0 || distribution.spacing <= 0.0f)
        return 0;
    if (distribution.jitter < 0.0f) distribution.jitter = 0.0f;
    if (distribution.jitter > 0.95f) distribution.jitter = 0.95f;

    float rowSpacing = distribution.spacing * 0.8660254f; // Equilateral hexagonal honeycomb row step
    int columns = (int)ceilf((distribution.maxBounds.x - distribution.minBounds.x) /
                             distribution.spacing);
    int rows = (int)ceilf((distribution.maxBounds.y - distribution.minBounds.y) /
                          rowSpacing);
    unsigned int rng = distribution.seed ? distribution.seed : 1u;
    int count = 0;
    for (int row = 0; row < rows && count < maxCount; row++) {
        float rowOffset = (row & 1) ? (distribution.spacing * 0.5f) : 0.0f;
        for (int column = 0; column < columns && count < maxCount; column++) {
            float jx = (Nature_Random01(&rng) - 0.5f) * distribution.spacing * distribution.jitter;
            float jz = (Nature_Random01(&rng) - 0.5f) * rowSpacing * distribution.jitter;
            float x = distribution.minBounds.x + column * distribution.spacing + rowOffset + jx;
            float z = distribution.minBounds.y + (row + 0.5f) * rowSpacing + jz;
            float density = densityFn ? densityFn(x, z, userData) : 1.0f;
            if (density < 0.0f) density = 0.0f;
            if (density > 1.0f) density = 1.0f;
            if (Nature_Random01(&rng) > density)
                continue;

            float y = groundCenter.y;
            if (ground && ground->ready)
                y = MapProp_SampleGroundHeight(ground, groundCenter, x, z);
            MapMeadowPlacement *placement = &outPlacements[count++];
            placement->position = (Vector3){x, y + distribution.yOffset, z};
            placement->radius = distribution.minRadius
                + (distribution.maxRadius - distribution.minRadius) * Nature_Random01(&rng);
            placement->height = distribution.minHeight
                + (distribution.maxHeight - distribution.minHeight) * Nature_Random01(&rng);
            placement->rotationDeg = Nature_Random01(&rng) * 360.0f;
            placement->phase = Nature_Random01(&rng);
        }
    }
    return count;
}

static inline Vector3 Nature_EvalCubicBezier(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float omt = 1.0f - t;
    float omt2 = omt * omt;
    float omt3 = omt2 * omt;
    float t2 = t * t;
    float t3 = t2 * t;
    return (Vector3){
        omt3 * p0.x + 3.0f * omt2 * t * p1.x + 3.0f * omt * t2 * p2.x + t3 * p3.x,
        omt3 * p0.y + 3.0f * omt2 * t * p1.y + 3.0f * omt * t2 * p2.y + t3 * p3.y,
        omt3 * p0.z + 3.0f * omt2 * t * p1.z + 3.0f * omt * t2 * p2.z + t3 * p3.z
    };
}

static inline Vector3 Nature_EvalCubicBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float omt = 1.0f - t;
    float c0 = 3.0f * omt * omt;
    float c1 = 6.0f * omt * t;
    float c2 = 3.0f * t * t;
    Vector3 v0 = Vector3Subtract(p1, p0);
    Vector3 v1 = Vector3Subtract(p2, p1);
    Vector3 v2 = Vector3Subtract(p3, p2);
    return (Vector3){
        c0 * v0.x + c1 * v1.x + c2 * v2.x,
        c0 * v0.y + c1 * v1.y + c2 * v2.y,
        c0 * v0.z + c1 * v1.z + c2 * v2.z
    };
}

static Model Nature_BuildMeadowChunk(const MapMeadowPlacement *placements, int count,
                                     MapMeadowStyle style, float minX, float maxX,
                                     float minZ, float maxZ, int sampleStride,
                                     int bladesPerClump, int bladeSegments,
                                     float widthMultiplier, int *outPlacementCount)
{
    int selected = 0;
    int ordinal = 0;
    for (int i = 0; i < count; i++) {
        Vector3 p = placements[i].position;
        if (p.x < minX || p.x >= maxX || p.z < minZ || p.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) == 0)
            selected++;
    }
    if (outPlacementCount) *outPlacementCount = selected;
    if (selected <= 0) return (Model){0};

    int verticesPerBlade = (bladeSegments - 1) * 6 + 3;
    int extraPerClump = style.hasPlumes ? 18 : 0;
    int vertexCount = selected * (bladesPerClump * verticesPerBlade + extraPerClump);
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    ordinal = 0;
    for (int i = 0; i < count; i++) {
        const MapMeadowPlacement *clump = &placements[i];
        if (clump->position.x < minX || clump->position.x >= maxX ||
            clump->position.z < minZ || clump->position.z >= maxZ)
            continue;
        if ((ordinal++ % sampleStride) != 0)
            continue;
        const float golden = 2.39996323f;
        for (int blade = 0; blade < bladesPerClump; blade++) {
            bool isReed = (clump->height > 0.95f);
            float clumpAngle = clump->rotationDeg * DEG2RAD;

            // Natural per-blade organic variations (Structured Procedural Variation)
            float bHash = sinf((float)(i * 43 + blade * 23)) * 43758.5453f;
            bHash -= floorf(bHash);
            float bHash2 = sinf((float)(i * 67 + blade * 37)) * 28461.1273f;
            bHash2 -= floorf(bHash2);
            float bHash3 = sinf((float)(i * 89 + blade * 13)) * 19283.4721f;
            bHash3 -= floorf(bHash3);

            float flowX = cosf(clumpAngle);
            float flowZ = sinf(clumpAngle);

            float height = 0.0f;
            float width = 0.0f;
            float lean = 0.0f;
            float droopY = 0.0f;
            float bladeLeanAngle = 0.0f;
            Vector3 pBase = {0};
            Vector3 pP1 = {0}, pP2 = {0}, pP3 = {0};
            Color bladeRoot = {0};
            Color bladeTip = {0};

            if (isReed) {
                float baseAngle = (float)blade * (2.0f * PI / (float)bladesPerClump);
                float bladeAzimuth = baseAngle + (bHash - 0.5f) * 0.95f;
                float radX = cosf(bladeAzimuth);
                float radZ = sinf(bladeAzimuth);

                float windWeight = 0.65f;
                float radWeight = 1.0f - windWeight;
                float combX = radX * radWeight + flowX * windWeight;
                float combZ = radZ * radWeight + flowZ * windWeight;
                float combLen = sqrtf(combX * combX + combZ * combZ);
                if (combLen < 0.01f) { combX = flowX; combZ = flowZ; combLen = 1.0f; }
                bladeLeanAngle = atan2f(combZ / combLen, combX / combLen);

                float collarRadius = clump->radius * 0.20f * (0.75f + 0.50f * bHash3);
                float bx = clump->position.x + radX * collarRadius;
                float bz = clump->position.z + radZ * collarRadius;

                float tierFrac = (float)blade / (float)bladesPerClump;
                float lengthScale = 0.85f + 0.30f * sinf((float)(i * 13 + blade * 7));
                height = clump->height * lengthScale;
                width = clump->radius * style.bladeWidthScale * widthMultiplier * (0.82f + 0.36f * bHash2);
                lean = height * (0.35f + 0.20f * tierFrac);
                droopY = height * (0.06f + 0.18f * bHash2);

                pBase = (Vector3){bx, clump->position.y, bz};
                pP1 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.10f,
                    pBase.y + height * 0.38f,
                    bz + sinf(bladeLeanAngle) * lean * 0.10f
                };
                pP2 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.44f,
                    pBase.y + height * 0.74f,
                    bz + sinf(bladeLeanAngle) * lean * 0.44f
                };
                pP3 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.95f,
                    pBase.y + (height * 0.86f - droopY),
                    bz + sinf(bladeLeanAngle) * lean * 0.95f
                };

                bool isOuterDrySheath = (blade < 2);
                if (isOuterDrySheath) {
                    bladeRoot = (Color){42, 34, 18, 255};
                    bladeTip  = (Color){192, 160, 82, 255};
                } else {
                    bladeRoot = (Color){24, 40, 18, 255};
                    bladeTip  = (Color){152, 196, 68, 255};
                }
            } else if (bladesPerClump <= 2 && bladeSegments <= 2) {
                // Two splayed silhouettes keep distant clumps from forming
                // parallel diagonal strokes across the whole meadow.
                float splay = bladesPerClump == 1 ? 0.0f : (blade == 0 ? -0.54f : 0.54f);
                bladeLeanAngle = clumpAngle + splay
                               + (bHash - 0.5f) * 0.46f;
                float bx = clump->position.x + cosf(bladeLeanAngle) * clump->radius * 0.09f;
                float bz = clump->position.z + sinf(bladeLeanAngle) * clump->radius * 0.09f;

                height = clump->height * (0.83f + 0.22f * bHash2);
                width = clump->radius * style.bladeWidthScale * widthMultiplier * 0.86f;
                lean = height * (0.35f + 0.13f * bHash3);
                droopY = height * 0.06f;

                pBase = (Vector3){bx, clump->position.y, bz};
                pP1 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.08f, pBase.y + height * 0.38f, bz + sinf(bladeLeanAngle) * lean * 0.08f};
                pP2 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.40f, pBase.y + height * 0.74f, bz + sinf(bladeLeanAngle) * lean * 0.40f};
                pP3 = (Vector3){bx + cosf(bladeLeanAngle) * lean * 0.90f, pBase.y + (height * 0.88f - droopY), bz + sinf(bladeLeanAngle) * lean * 0.90f};

                bladeRoot = style.rootColor;
                bladeTip = style.tipColor;
            } else {
                // Volumetric clump architecture, including reduced far LOD.
                // Blades emerge from a collar and spread beyond the wind axis.
                float baseAzimuth = ((float)blade / (float)bladesPerClump) * (2.0f * PI)
                                  + (bHash - 0.5f) * 0.45f;
                float collarRadius = clump->radius * (0.16f + 0.12f * bHash3);
                float bx = clump->position.x + cosf(baseAzimuth) * collarRadius;
                float bz = clump->position.z + sinf(baseAzimuth) * collarRadius;

                float radX = cosf(baseAzimuth);
                float radZ = sinf(baseAzimuth);
                float windWeight = 0.58f;
                float combX = radX * (1.0f - windWeight) + flowX * windWeight;
                float combZ = radZ * (1.0f - windWeight) + flowZ * windWeight;
                float combLen = sqrtf(combX * combX + combZ * combZ);
                if (combLen < 0.01f) { combX = flowX; combZ = flowZ; combLen = 1.0f; }
                bladeLeanAngle = atan2f(combZ / combLen, combX / combLen);

                float tierFrac = (float)blade / (float)(bladesPerClump - 1);
                // Mix short inner leaves with long outer blades so nearby
                // clumps do not collapse into one repeated fan silhouette.
                float lengthScale = 0.76f + 0.31f * tierFrac + 0.17f * (bHash2 - 0.5f);
                height = clump->height * lengthScale;
                width = clump->radius * style.bladeWidthScale * widthMultiplier *
                        (0.76f + 0.34f * bHash3 + 0.10f * (1.0f - tierFrac));
                lean = height * (0.34f + 0.16f * tierFrac + 0.08f * bHash);
                droopY = height * (0.04f + 0.08f * tierFrac + 0.04f * bHash3);
                if (bladeSegments == 1) {
                    // A full-length distant triangle rasterizes as a thin,
                    // bright diagonal. Keep its area in a shorter, wider tuft.
                    height *= 0.78f;
                    width *= 1.28f;
                    lean *= 0.67f;
                    droopY *= 0.65f;
                }

                // Cantilever progressive Bézier curve (monotonically increasing curvature, zero kinks)
                pBase = (Vector3){bx, clump->position.y, bz};
                pP1 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.08f,
                    pBase.y + height * 0.38f,
                    bz + sinf(bladeLeanAngle) * lean * 0.08f
                };
                pP2 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.40f,
                    pBase.y + height * 0.74f,
                    bz + sinf(bladeLeanAngle) * lean * 0.40f
                };
                pP3 = (Vector3){
                    bx + cosf(bladeLeanAngle) * lean * 0.90f,
                    pBase.y + fmaxf(height * 0.28f, height * 0.88f - droopY),
                    bz + sinf(bladeLeanAngle) * lean * 0.90f
                };

                // Coherent Macro Seedhead Biome Field (scale ~18m):
                float strawNoise = sinf(clump->position.x * 0.16f + clump->position.z * 0.12f + 1.7f) * 0.5f
                                 + sinf(clump->position.x * -0.10f + clump->position.z * 0.22f + 0.5f) * 0.5f;
                bool isSeedhead = (strawNoise > 0.40f) && (blade == bladesPerClump - 1) && (bHash3 > 0.25f);
                if (isSeedhead) {
                    bladeRoot = (Color){46, 60, 22, 255};   // warm olive-gold sheath
                    bladeTip  = (Color){208, 182, 85, 255};  // ripe golden-amber wheat straw tip
                } else {
                    float colorField = sinf(clump->position.x * 0.19f + clump->position.z * 0.07f) * 0.55f
                                     + sinf(clump->position.z * 0.15f - clump->position.x * 0.05f) * 0.45f;
                    float tone = 1.0f + colorField * 0.11f + (bHash - 0.5f) * 0.15f;
                    float warmth = colorField * 0.08f + (bHash3 - 0.5f) * 0.06f;
                    int rR = (int)(style.rootColor.r * tone * (1.0f + warmth));
                    int rG = (int)(style.rootColor.g * tone);
                    int rB = (int)(style.rootColor.b * tone * (1.0f - warmth));
                    int tR = (int)(style.tipColor.r * tone * (1.0f + warmth));
                    int tG = (int)(style.tipColor.g * tone);
                    int tB = (int)(style.tipColor.b * tone * (1.0f - warmth));
                    bladeRoot = (Color){(unsigned char)fminf(255, fmaxf(0, rR)),
                                        (unsigned char)fminf(255, fmaxf(0, rG)),
                                        (unsigned char)fminf(255, fmaxf(0, rB)), 255};
                    bladeTip  = (Color){(unsigned char)fminf(255, fmaxf(0, tR)),
                                        (unsigned char)fminf(255, fmaxf(0, tG)),
                                        (unsigned char)fminf(255, fmaxf(0, tB)), 255};
                }
            }

            // Base transverse vector fallback
            Vector3 baseSide = (Vector3){-sinf(bladeLeanAngle), 0.0f, cosf(bladeLeanAngle)};
            Vector3 terrainNormal = (Vector3){0.0f, 1.0f, 0.0f};

            for (int segment = 0; segment < bladeSegments; segment++) {
                // Long needle triangles create the bright leaf-tip streaks.
                // Give the pointed triangle only the last 18% of the curve.
                float tipStart = bladeSegments > 1 ? 0.82f : 0.0f;
                float t0 = segment == bladeSegments - 1 ? tipStart
                         : tipStart * (float)segment / (float)(bladeSegments - 1);
                float t1 = segment == bladeSegments - 1 ? 1.0f
                         : tipStart * (float)(segment + 1) / (float)(bladeSegments - 1);

                Vector3 center0 = Nature_EvalCubicBezier(pBase, pP1, pP2, pP3, t0);
                Vector3 center1 = Nature_EvalCubicBezier(pBase, pP1, pP2, pP3, t1);

                // Section 1.2: Exact Tangent T(t) = dB(t)/dt
                Vector3 T0 = Vector3Normalize(Nature_EvalCubicBezierTangent(pBase, pP1, pP2, pP3, t0));
                Vector3 T1 = Vector3Normalize(Nature_EvalCubicBezierTangent(pBase, pP1, pP2, pP3, t1));

                // Section 1.2: Transverse Vector S(t) = normalize(T(t) x N_terrain)
                Vector3 crossS0 = Vector3CrossProduct(T0, terrainNormal);
                float lenS0 = Vector3Length(crossS0);
                Vector3 S0 = (lenS0 > 0.001f) ? Vector3Scale(crossS0, 1.0f / lenS0) : baseSide;

                Vector3 crossS1 = Vector3CrossProduct(T1, terrainNormal);
                float lenS1 = Vector3Length(crossS1);
                Vector3 S1 = (lenS1 > 0.001f) ? Vector3Scale(crossS1, 1.0f / lenS1) : baseSide;

                // Section 1.3: Geometric Normal N_geo(t) = normalize(S(t) x T(t))
                Vector3 Ngeo0 = Vector3Normalize(Vector3CrossProduct(S0, T0));
                if (Ngeo0.y < 0.0f) Ngeo0 = Vector3Negate(Ngeo0);
                Ngeo0.y = fmaxf(Ngeo0.y, 0.25f);
                Ngeo0 = Vector3Normalize(Ngeo0);

                Vector3 Ngeo1 = Vector3Normalize(Vector3CrossProduct(S1, T1));
                if (Ngeo1.y < 0.0f) Ngeo1 = Vector3Negate(Ngeo1);
                Ngeo1.y = fmaxf(Ngeo1.y, 0.25f);
                Ngeo1 = Vector3Normalize(Ngeo1);

                // Botanical Spear Blade Profile: fuller mid-body for lush coverage, needle tip
                float profile0 = (0.85f + 0.25f * sinf(PI * t0)) * (1.0f - powf(t0, 1.8f));
                float profile1 = (0.85f + 0.25f * sinf(PI * t1)) * (1.0f - powf(t1, 1.8f));
                float halfW0 = (width * 0.5f) * fmaxf(profile0, 0.08f);
                float halfW1 = (width * 0.5f) * fmaxf(profile1, 0.03f);

                // Section 1.2: V-shaped cross section (creased spine, winged edges)
                Vector3 fold0 = Vector3Scale(Ngeo0, halfW0 * 0.18f);
                Vector3 fold1 = Vector3Scale(Ngeo1, halfW1 * 0.18f);

                Vector3 p0 = Vector3Add(Vector3Subtract(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p1 = Vector3Add(Vector3Add(center0, Vector3Scale(S0, halfW0)), fold0);
                Vector3 p2 = Vector3Add(Vector3Add(center1, Vector3Scale(S1, halfW1)), fold1);
                Vector3 p3 = Vector3Add(Vector3Subtract(center1, Vector3Scale(S1, halfW1)), fold1);

                // Root ambient occlusion: soft, natural ground grounding without harsh pitch-black spots
                float occ0 = (t0 < 0.22f) ? (0.88f + 0.12f * (t0 / 0.22f)) : 1.0f;
                float occ1 = (t1 < 0.22f) ? (0.88f + 0.12f * (t1 / 0.22f)) : 1.0f;
                Color color0 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t0), occ0);
                Color color1 = Nature_ScaleColor(Nature_LerpColor(bladeRoot, bladeTip, t1), occ1);

                // Ghost of Tsushima / AAA Reference: Bent Vertex Normals
                // Blend polygon normal with Spherical Clump Normal + Upward Ground Normal
                Vector3 clumpSphereCenter = (Vector3){clump->position.x, clump->position.y - 0.04f, clump->position.z};

                Vector3 dSphere0L = Vector3Normalize(Vector3Subtract(p0, clumpSphereCenter));
                Vector3 dSphere0R = Vector3Normalize(Vector3Subtract(p1, clumpSphereCenter));
                Vector3 dSphere1L = Vector3Normalize(Vector3Subtract(p3, clumpSphereCenter));
                Vector3 dSphere1R = Vector3Normalize(Vector3Subtract(p2, clumpSphereCenter));
                Vector3 dSphereTip = Vector3Normalize(Vector3Subtract(center1, clumpSphereCenter));

                Vector3 nTarget0L = Vector3Normalize(Vector3Lerp(dSphere0L, terrainNormal, 0.45f));
                Vector3 nTarget0R = Vector3Normalize(Vector3Lerp(dSphere0R, terrainNormal, 0.45f));
                Vector3 nTarget1L = Vector3Normalize(Vector3Lerp(dSphere1L, terrainNormal, 0.45f));
                Vector3 nTarget1R = Vector3Normalize(Vector3Lerp(dSphere1R, terrainNormal, 0.45f));
                Vector3 nTargetTip = Vector3Normalize(Vector3Lerp(dSphereTip, terrainNormal, 0.55f));

                // Blade edge transverse tilt
                Vector3 nMesh0L = Vector3Normalize(Vector3Subtract(Ngeo0, Vector3Scale(S0, 0.35f)));
                Vector3 nMesh0R = Vector3Normalize(Vector3Add(Ngeo0, Vector3Scale(S0, 0.35f)));
                Vector3 nMesh1L = Vector3Normalize(Vector3Subtract(Ngeo1, Vector3Scale(S1, 0.32f)));
                Vector3 nMesh1R = Vector3Normalize(Vector3Add(Ngeo1, Vector3Scale(S1, 0.32f)));

                // 72% Bent Normal blending for silky, continuous velvet lighting
                Vector3 nL0 = Vector3Normalize(Vector3Lerp(nMesh0L, nTarget0L, 0.72f));
                Vector3 nR0 = Vector3Normalize(Vector3Lerp(nMesh0R, nTarget0R, 0.72f));
                Vector3 nL1 = Vector3Normalize(Vector3Lerp(nMesh1L, nTarget1L, 0.72f));
                Vector3 nR1 = Vector3Normalize(Vector3Lerp(nMesh1R, nTarget1R, 0.72f));
                Vector3 nTip = Vector3Normalize(Vector3Lerp(Ngeo1, nTargetTip, 0.78f));

                if (style.hasPlumes) {
                    if (segment == bladeSegments - 1) {
                        Nature_SetVertex(&mesh, cursor++, p0, nL0, clump->phase, t0, color0);
                        Nature_SetVertex(&mesh, cursor++, p1, nR0, clump->phase, t0, color0);
                        Nature_SetVertex(&mesh, cursor++, center1, nTip, clump->phase, 1.0f, color1);
                    } else {
                        Nature_AddCurvedBladeQuad(&mesh, &cursor, p0, p1, p2, p3,
                                                  nL0, nR0, nR1, nL1,
                                                  clump->phase, t0, t1, color0, color1);
                    }
                } else if (segment == bladeSegments - 1) {
                    Nature_AddCurvedBladeTip(&mesh, &cursor, p0, p1, center1,
                                             nL0, nR0, nTip,
                                             clump->phase, t0, color0, color1);
                } else {
                    Nature_AddCurvedBladeQuad(&mesh, &cursor, p0, p1, p2, p3,
                                              nL0, nR0, nR1, nL1,
                                              clump->phase, t0, t1, color0, color1);
                }
            }
        }

        // Shore reeds: 3D fluted silky plumes and arching lateral leaves (54 + 12 = 66 vertices)
        if (style.hasPlumes) {
            float caneHeight = clump->height * 1.35f;
            float caneAngle = clump->rotationDeg * DEG2RAD + 0.22f * sinf((float)i * 1.7f);
            float caneLean = caneHeight * 0.32f;
            Vector3 caneP0 = {clump->position.x, clump->position.y, clump->position.z};
            Vector3 caneP1 = {clump->position.x + cosf(caneAngle) * caneLean * 0.45f,
                              clump->position.y + caneHeight * 0.55f,
                              clump->position.z + sinf(caneAngle) * caneLean * 0.45f};
            Vector3 caneP2 = {clump->position.x + cosf(caneAngle) * caneLean,
                              clump->position.y + caneHeight,
                              clump->position.z + sinf(caneAngle) * caneLean};

            // 4 knot points along curved plume spindle (t = 0.65 to 1.0)
            float tKnots[4] = {0.65f, 0.78f, 0.90f, 1.00f};
            Vector3 plPts[4];
            for (int k = 0; k < 4; k++) {
                float tk = tKnots[k];
                float omtk = 1.0f - tk;
                plPts[k] = (Vector3){
                    omtk*omtk*caneP0.x + 2*omtk*tk*caneP1.x + tk*tk*caneP2.x,
                    omtk*omtk*caneP0.y + 2*omtk*tk*caneP1.y + tk*tk*caneP2.y,
                    omtk*omtk*caneP0.z + 2*omtk*tk*caneP1.z + tk*tk*caneP2.z
                };
            }

            // Parametric Reed Plume Density Envelope: R(u) = R_max * sin(pi * u^0.8)^0.6
            // Volumetric envelope with feathery fiber alpha texture mapping
            // Single flat textured plume quad (1 quad: 6 vertices)
            float stripAngle = caneAngle + 1.5707963f;
            Vector3 side = {-sinf(stripAngle), 0.0f, cosf(stripAngle)};
            Vector3 norm = Vector3Normalize((Vector3){cosf(stripAngle), 0.25f, sinf(stripAngle)});

            Vector3 qBase = plPts[0];
            Vector3 qTip = plPts[3];
            float plumeH = Vector3Length(Vector3Subtract(qTip, qBase));
            float halfW = fmaxf(0.12f, plumeH * 0.28f);

            Vector3 q0 = {qBase.x - side.x * halfW, qBase.y, qBase.z - side.z * halfW};
            Vector3 q1 = {qBase.x + side.x * halfW, qBase.y, qBase.z + side.z * halfW};
            Vector3 q2 = {qTip.x + side.x * halfW * 0.70f, qTip.y, qTip.z + side.z * halfW * 0.70f};
            Vector3 q3 = {qTip.x - side.x * halfW * 0.70f, qTip.y, qTip.z - side.z * halfW * 0.70f};

            Color c0 = {245, 238, 220, 255};
            Color c1 = {255, 255, 255, 255};

            if (style.texturePath != NULL) {
                Nature_AddTexturedQuadUV(&mesh, &cursor, q0, q1, q2, q3, norm,
                                         clump->phase, 0.52f, 1.0f,
                                         c0, c1, 0.0f, 0.0f, 1.0f, 1.0f);
            } else {
                Nature_AddQuad(&mesh, &cursor, q0, q1, q2, q3, norm,
                               clump->phase, 0.52f, 1.0f, c0, c1);
            }

            // Two lateral arching leaves along reed stalk
            float tL0 = 0.32f, tL1 = 0.52f;
            float omtL0 = 1.0f - tL0, omtL1 = 1.0f - tL1;
            Vector3 leafRoot0 = {
                omtL0*omtL0*caneP0.x + 2*omtL0*tL0*caneP1.x + tL0*tL0*caneP2.x,
                omtL0*omtL0*caneP0.y + 2*omtL0*tL0*caneP1.y + tL0*tL0*caneP2.y,
                omtL0*omtL0*caneP0.z + 2*omtL0*tL0*caneP1.z + tL0*tL0*caneP2.z
            };
            Vector3 leafRoot1 = {
                omtL1*omtL1*caneP0.x + 2*omtL1*tL1*caneP1.x + tL1*tL1*caneP2.x,
                omtL1*omtL1*caneP0.y + 2*omtL1*tL1*caneP1.y + tL1*tL1*caneP2.y,
                omtL1*omtL1*caneP0.z + 2*omtL1*tL1*caneP1.z + tL1*tL1*caneP2.z
            };
            float lAng0 = caneAngle + 1.42f;
            float lAng1 = caneAngle - 1.42f;
            Vector3 lDir0 = {cosf(lAng0), 0.0f, sinf(lAng0)};
            Vector3 lDir1 = {cosf(lAng1), 0.0f, sinf(lAng1)};
            Vector3 lSide0 = {-lDir0.z * 0.024f, 0.0f, lDir0.x * 0.024f};
            Vector3 lSide1 = {-lDir1.z * 0.022f, 0.0f, lDir1.x * 0.022f};
            Vector3 lTip0 = {leafRoot0.x + lDir0.x * 0.45f, leafRoot0.y - 0.12f, leafRoot0.z + lDir0.z * 0.45f};
            Vector3 lTip1 = {leafRoot1.x + lDir1.x * 0.40f, leafRoot1.y - 0.10f, leafRoot1.z + lDir1.z * 0.40f};
            Color leafCol = {88, 122, 50, 255};
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(leafRoot0, lSide0), Vector3Add(leafRoot0, lSide0),
                           Vector3Add(lTip0, Vector3Scale(lSide0, 0.15f)), Vector3Subtract(lTip0, Vector3Scale(lSide0, 0.15f)),
                           (Vector3){0, 1, 0}, clump->phase, 0.3f, 0.5f, leafCol, Nature_ScaleColor(leafCol, 1.15f));
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(leafRoot1, lSide1), Vector3Add(leafRoot1, lSide1),
                           Vector3Add(lTip1, Vector3Scale(lSide1, 0.15f)), Vector3Subtract(lTip1, Vector3Scale(lSide1, 0.15f)),
                           (Vector3){0, 1, 0}, clump->phase, 0.4f, 0.6f, leafCol, Nature_ScaleColor(leafCol, 1.15f));
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(style.texturePath != NULL));
}

MapMeadowSurface MapProp_CreateMeadow(const MapMeadowPlacement *placements, int count,
                                      MapMeadowStyle style)
{
    MapMeadowSurface meadow = {0};
    if (!placements || count <= 0) return meadow;
    if (style.bladesPerClump < 1) style.bladesPerClump = 1;
    if (style.bladesPerClump > 10) style.bladesPerClump = 10;
    if (style.bladeSegments < 1) style.bladeSegments = 1;
    if (style.bladeSegments > 6) style.bladeSegments = 6;
    if (style.bladeWidthScale <= 0.0f) style.bladeWidthScale = 0.24f;
    if (style.chunkSize <= 0.0f) style.chunkSize = 12.0f;
    if (style.alphaCutoff <= 0.0f) style.alphaCutoff = 0.42f;
    if (style.alphaCutoff > 0.9f) style.alphaCutoff = 0.9f;
    if (style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_HIGH)
        (void)NatureShadow_GetShader();
    Texture2D foliageTexture = {0};
    bool textured = style.texturePath != NULL;
    if (textured) {
        foliageTexture = ResourceManager_LoadTexture(style.texturePath);
        GenTextureMipmaps(&foliageTexture);
        SetTextureFilter(foliageTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(foliageTexture, TEXTURE_WRAP_CLAMP);
    }

    float minX = placements[0].position.x;
    float maxX = minX;
    float minZ = placements[0].position.z;
    float maxZ = minZ;
    for (int i = 1; i < count; i++) {
        if (placements[i].position.x < minX) minX = placements[i].position.x;
        if (placements[i].position.x > maxX) maxX = placements[i].position.x;
        if (placements[i].position.z < minZ) minZ = placements[i].position.z;
        if (placements[i].position.z > maxZ) maxZ = placements[i].position.z;
    }
    maxX += 0.001f;
    maxZ += 0.001f;
    int columns = (int)ceilf((maxX - minX) / style.chunkSize);
    int rows = (int)ceilf((maxZ - minZ) / style.chunkSize);
    if (columns < 1) columns = 1;
    if (rows < 1) rows = 1;
    int capacity = columns * rows;
    meadow.chunks = MemAlloc((unsigned int)capacity * sizeof(MapMeadowChunk));
    meadow.lodDistance = style.lodDistance;
    meadow.midLodDistance = style.midLodDistance > 0.0f ? style.midLodDistance
                          : (style.lodDistance > 0.0f ? style.lodDistance * 0.45f : 0.0f);
    meadow.drawDistance = style.drawDistance;
    meadow.shadowDistance = style.shadowDistance;
    bool buildContactShadows = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_MED;
    bool buildRealShadowLod = style.shadowDistance > 0.0f && GfxQuality_Get() >= GFX_HIGH;

    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            float x0 = minX + column * style.chunkSize;
            float x1 = x0 + style.chunkSize;
            float z0 = minZ + row * style.chunkSize;
            float z1 = z0 + style.chunkSize;
            int nearCount = 0;
            Model nearModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                style.bladesPerClump, style.bladeSegments, 1.0f, &nearCount);
            if (nearCount <= 0)
                continue;

            // Mid LOD: intermediate distance (e.g. 10m - 22m).
            // Uses fewer blades and segments with slight widening (1.22x) to preserve silhouette and volume
            // while drastically reducing subpixel polygon workload and overdraw.
            int midBlades = (style.bladesPerClump >= 6) ? 4 : (style.bladesPerClump >= 4 ? 3 : 2);
            int midSegments = (style.bladeSegments >= 3) ? 2 : 1;
            int midCount = 0;
            Model midModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                midBlades, midSegments, 1.22f, &midCount);

            // Preserve coverage: removing every second clump turns a meadow
            // into isolated spikes. Far LOD reduces each clump instead.
            // Compensate for the lost blade count in projected coverage. Thin
            // far blades reveal the dark ground as a stippled LOD boundary.
            // Three distinct silhouettes retain the clump shape at one segment.
            int farBlades = 3;
            int farCount = 0;
            Model farModel = Nature_BuildMeadowChunk(
                placements, count, style, x0, x1, z0, z1, 1,
                farBlades, 1, 1.65f, &farCount);
            Model shadowModel = {0};
            // Eliminate crude 6-vertex trapezoid wedges.
            // Grass uses real dynamic shadow map casting via realShadowModel.
            Model realShadowModel = {0};
            int realShadowCount = 0;
            if (buildRealShadowLod)
                realShadowModel = Nature_BuildMeadowChunk(
                    placements, count, style, x0, x1, z0, z1, 1,
                    3, 2, 1.30f, &realShadowCount);
            MapMeadowChunk *chunk = &meadow.chunks[meadow.chunkCount++];
            if (textured) {
                nearModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                if (midCount > 0)
                    midModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
                if (realShadowCount > 0)
                    realShadowModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = foliageTexture;
            }
            *chunk = (MapMeadowChunk){
                .nearModel = nearModel,
                .midModel = midModel,
                .farModel = farModel,
                .shadowModel = shadowModel,
                .realShadowModel = realShadowModel,
                .center = {(x0 + x1) * 0.5f, 0.0f, (z0 + z1) * 0.5f},
                .radius = style.chunkSize * 0.72f + 1.5f,
                .lodLevel = 0,
                .farLod = false,
                .midReady = (midCount > 0),
                .shadowReady = shadowModel.meshCount > 0,
                .realShadowReady = realShadowCount > 0,
                .ready = true,
            };
        }
    }
    if (meadow.chunkCount <= 0) {
        MemFree(meadow.chunks);
        meadow.chunks = NULL;
        return meadow;
    }
    meadow.ready = true;
    meadow.textured = textured;
    meadow.alphaCutoff = style.alphaCutoff;
    return meadow;
}

static bool Nature_IsChunkVisible(Vector3 center, float radius)
{
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    float forwardLength = Vector3Length(forward);
    if (forwardLength < 0.001f)
        return true;
    forward = Vector3Scale(forward, 1.0f / forwardLength);
    Vector3 right = Vector3CrossProduct(forward, camera.up);
    float rightLength = Vector3Length(right);
    if (rightLength < 0.001f)
        return true;
    right = Vector3Scale(right, 1.0f / rightLength);
    Vector3 viewUp = Vector3Normalize(Vector3CrossProduct(right, forward));
    Vector3 toCenter = Vector3Subtract(center, camera.position);
    float depth = Vector3DotProduct(toCenter, forward);
    if (depth < -radius)
        return false;

    float aspect = (float)GetScreenWidth() / (float)fmaxf((float)GetScreenHeight(), 1.0f);
    float halfVertical;
    float halfHorizontal;
    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        halfVertical = camera.fovy * 0.5f;
        halfHorizontal = halfVertical * aspect;
    } else {
        float positiveDepth = fmaxf(depth, 0.0f);
        halfVertical = tanf(camera.fovy * DEG2RAD * 0.5f) * positiveDepth;
        halfHorizontal = halfVertical * aspect;
    }
    float sideDistance = fabsf(Vector3DotProduct(toCenter, right));
    float verticalDistance = fabsf(Vector3DotProduct(toCenter, viewUp));
    return sideDistance <= halfHorizontal + radius &&
           verticalDistance <= halfVertical + radius;
}

// Shadow casters belong to the cascade, not to a camera-distance sphere. Keep
// this rejection deliberately conservative and backend-independent: applying
// the CPU matrix convention to a GPU-ready light VP caused valid vegetation to
// be rejected on Vulkan even though its real depth draw was otherwise correct.
static bool Nature_IntersectsDynamicShadowCoverage(Vector3 center, float radius)
{
    Vector3 focus = EnvShadow_GetFocus();
    Vector3 sun = Vector3Normalize(Environment_GetSunDirection());
    float extent = fmaxf(EnvShadow_GetHalfExtent(), 8.0f);
    // The tilted light-space vertical axis covers a wider ground footprint as
    // the sun gets lower. A horizontal bounding circle is intentionally wider
    // than the true oriented cascade, trading a few casters for zero holes.
    float groundRadius = extent / fmaxf(fabsf(sun.y), 0.32f);
    float dx = center.x - focus.x;
    float dz = center.z - focus.z;
    float limit = groundRadius + radius * 1.12f + 2.0f;
    return dx * dx + dz * dz <= limit * limit;
}

void MapProp_DrawMeadow(MapMeadowSurface *meadow, Vector3 worldOffset, float time,
                        Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready) return;
    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH ? 1.0f
                     : quality == GFX_MED ? 0.84f
                     : quality == GFX_LOW ? 0.68f : 0.55f;
    float lodScale = quality >= GFX_HIGH ? 1.0f
                   : quality == GFX_MED ? 0.84f : 0.68f;

    // Camera focal-distance and zoom-aware LOD adjustment:
    // In third-person/orbit view, camera.position is separated from the target by focal distance.
    // Offsetting with horizontal focal distance ensures meadow->lodDistance measures radius around the player/target.
    // Scaling with zoomFactor ensures zooming in (narrower FOV or closer view) extends Near LOD coverage on screen.
    float focalDx = camera.position.x - camera.target.x;
    float focalDz = camera.position.z - camera.target.z;
    float focalDistH = sqrtf(focalDx * focalDx + focalDz * focalDz);
    float fovyRad = fmaxf(camera.fovy, 15.0f) * DEG2RAD;
    float zoomFactor = tanf(45.0f * 0.5f * DEG2RAD) / tanf(fovyRad * 0.5f);
    if (zoomFactor < 0.8f) zoomFactor = 0.8f;
    if (zoomFactor > 2.5f) zoomFactor = 2.5f;

    float lodDistance = focalDistH + meadow->lodDistance * lodScale * zoomFactor;
    float drawDistance = (meadow->drawDistance > 0.0f) ? (focalDistH + meadow->drawDistance * rangeScale * zoomFactor) : 0.0f;
    float drawDistanceSq = drawDistance * drawDistance;
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        chunk->visibleThisFrame = false;
        s_natureRenderStats.meadowChunksTested++;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        if (!Nature_IsChunkVisible(center, chunk->radius)) {
            s_natureRenderStats.meadowFrustumCulled++;
            continue;
        }
        float dx = camera.position.x - center.x;
        float dz = camera.position.z - center.z;
        float distanceSq = dx * dx + dz * dz;
        if (drawDistance > 0.0f && distanceSq > drawDistanceSq) {
            s_natureRenderStats.meadowDistanceCulled++;
            continue;
        }
        s_natureRenderStats.meadowChunksVisible++;
        chunk->visibleThisFrame = true;

        if (lodDistance > 0.0f) {
            float spatialHash = sinf(chunk->center.x * 12.9898f + chunk->center.z * 78.233f);
            spatialHash = spatialHash - floorf(spatialHash);
            float farThreshold = lodDistance + (spatialHash - 0.5f) * 4.0f;
            float midThreshold = (meadow->midLodDistance > 0.0f && chunk->midReady) ?
                                 (focalDistH + meadow->midLodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 2.5f) : 0.0f;
            float hysteresis = quality >= GFX_HIGH ? 1.1f : 1.8f;
            float distance = sqrtf(distanceSq);

            if (midThreshold > 0.0f) {
                // 3-tier LOD with hysteresis to prevent edge thrashing
                if (chunk->lodLevel == 2) { // currently Far
                    if (distance < farThreshold - hysteresis) {
                        chunk->lodLevel = (distance < midThreshold - hysteresis) ? 0 : 1;
                    }
                } else if (chunk->lodLevel == 1) { // currently Mid
                    if (distance > farThreshold + hysteresis) {
                        chunk->lodLevel = 2;
                    } else if (distance < midThreshold - hysteresis) {
                        chunk->lodLevel = 0;
                    }
                } else { // currently Near
                    if (distance > farThreshold + hysteresis) {
                        chunk->lodLevel = 2;
                    } else if (distance > midThreshold + hysteresis) {
                        chunk->lodLevel = 1;
                    }
                }
            } else {
                // 2-tier fallback
                if (chunk->lodLevel == 2) {
                    if (distance < farThreshold - hysteresis)
                        chunk->lodLevel = 0;
                } else if (distance > farThreshold + hysteresis) {
                    chunk->lodLevel = 2;
                }
            }
            chunk->farLod = (chunk->lodLevel == 2);
        } else {
            chunk->lodLevel = 0;
            chunk->farLod = false;
        }

    }

    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_HIGH && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    if (useProjectedShadows && meadow->shadowDistance > 0.0f) {
        float shadowScale = quality >= GFX_HIGH ? 1.0f : 0.76f;
        float shadowDistance = meadow->shadowDistance * shadowScale;
        Shader shadowShader = FlowerShadow_GetShader();
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        for (int i = 0; i < meadow->chunkCount; i++) {
            MapMeadowChunk *chunk = &meadow->chunks[i];
            if (!chunk->visibleThisFrame || !chunk->shadowReady)
                continue;
            Vector3 center = Vector3Add(chunk->center, worldOffset);
            float dx = camera.position.x - center.x;
            float dz = camera.position.z - center.z;
            float visibleDistance = sqrtf(dx * dx + dz * dz) - chunk->radius;
            if (visibleDistance > shadowDistance) {
                s_natureRenderStats.meadowShadowDistanceCulled++;
                continue;
            }
            DrawModel(chunk->shadowModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowShadowDraws++;
        }
        EndBlendMode();
        rlEnableDepthMask();
    }

    Shader shader = Nature_GetShader(meadow->textured);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        meadow->textured, meadow->alphaCutoff,
                        NATURE_WIND_RESPONSE_GRASS);
    if (!meadow->textured) {
        float tipSoftening = 1.0f;
        int tipSofteningLoc = GetShaderLocation(shader, "u_grassTipSoftening");
        if (tipSofteningLoc >= 0)
            SetShaderValue(shader, tipSofteningLoc, &tipSoftening, SHADER_UNIFORM_FLOAT);
    }
    rlDisableBackfaceCulling();
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->visibleThisFrame)
            continue;
        if (chunk->lodLevel == 2) {
            DrawModel(chunk->farModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowFarDraws++;
        } else if (chunk->lodLevel == 1 && chunk->midReady) {
            DrawModel(chunk->midModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowMidDraws++;
        } else {
            DrawModel(chunk->nearModel, worldOffset, 1.0f, WHITE);
            s_natureRenderStats.meadowNearDraws++;
        }
    }
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_DrawMeadowShadowCasters(MapMeadowSurface *meadow, Vector3 worldOffset,
                                     float time, Vector2 windDirection, float windStrength)
{
    if (!meadow || !meadow->ready || GfxQuality_Get() < GFX_HIGH ||
        meadow->shadowDistance <= 0.0f ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(false))
        return;
    Shader shader = NatureShadow_GetShader();
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              meadow->textured, meadow->alphaCutoff,
                              NATURE_WIND_RESPONSE_GRASS);
    rlDisableBackfaceCulling();
    // The shadow box follows the gameplay focus, not the orbit camera. A
    // camera-centred test dropped casters at the far side of the visible box.
    Vector3 shadowFocus = EnvShadow_GetFocus();
    float maxDist = fmaxf(meadow->shadowDistance + 6.0f,
                          EnvShadow_GetHalfExtent() + 4.0f);
    for (int i = 0; i < meadow->chunkCount; i++) {
        MapMeadowChunk *chunk = &meadow->chunks[i];
        if (!chunk->realShadowReady)
            continue;
        Vector3 center = Vector3Add(chunk->center, worldOffset);
        float dx = center.x - shadowFocus.x;
        float dz = center.z - shadowFocus.z;
        float limit = maxDist + chunk->radius;
        if ((dx * dx + dz * dz) > limit * limit)
            continue;
        if (!Nature_IntersectsDynamicShadowCoverage(center, chunk->radius) &&
            !Nature_ShadowCasterFilterActive())
            continue;
        Shader previous = chunk->realShadowModel.materials[0].shader;
        chunk->realShadowModel.materials[0].shader = shader;
        DrawModel(chunk->realShadowModel, worldOffset, 1.0f, WHITE);
        chunk->realShadowModel.materials[0].shader = previous;
    }
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadMeadow(MapMeadowSurface *meadow)
{
    if (!meadow || !meadow->ready) return;
    for (int i = 0; i < meadow->chunkCount; i++) {
        UnloadModel(meadow->chunks[i].nearModel);
        if (meadow->chunks[i].midReady)
            UnloadModel(meadow->chunks[i].midModel);
        UnloadModel(meadow->chunks[i].farModel);
        if (meadow->chunks[i].shadowReady)
            UnloadModel(meadow->chunks[i].shadowModel);
        if (meadow->chunks[i].realShadowReady)
            UnloadModel(meadow->chunks[i].realShadowModel);
    }
    MemFree(meadow->chunks);
    meadow->chunks = NULL;
    meadow->chunkCount = 0;
    meadow->shadowDistance = 0.0f;
    meadow->ready = false;
}

static bool Nature_KeepFarFlower(const MapFlowerPlacement *flower)
{
    float hash = sinf(flower->position.x * 12.9898f + flower->position.z * 78.233f
                      + flower->phase * 37.719f) * 43758.5453f;
    return hash - floorf(hash) < 0.56f;
}

static Model Nature_BuildFlowerFarModel(const MapFlowerPlacement *placements, int count,
                                        bool texturedBloom,
                                        int atlasColumns, int atlasRows)
{
    // At distance, stems are sub-pixel and alpha-cutout bloom overdraw dominates.
    // Keep a stable world-space subset and slightly compensate bloom coverage.
    int selected = 0;
    for (int i = 0; i < count; i++) {
        if (Nature_KeepFarFlower(&placements[i]))
            selected++;
    }
    bool forceFirst = selected == 0;
    if (forceFirst) selected = 1;
    Mesh mesh = Nature_AllocMesh(selected * 6);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        if (!Nature_KeepFarFlower(flower) && !(forceFirst && i == 0))
            continue;
        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 4.1f;
        float lean = h * (0.025f + 0.055f * (0.5f + 0.5f * sinf((float)i * 2.37f)));
        Vector3 head = {base.x + cosf(leanAngle) * lean, base.y + h,
                        base.z + sinf(leanAngle) * lean};
        float stemAngle = flower->rotationDeg * DEG2RAD;
        float headTilt = 0.24f + 0.28f * (0.5f + 0.5f * sinf((float)i * 1.91f + phase));
        float tiltX = cosf(leanAngle) * headTilt;
        float tiltZ = sinf(leanAngle) * headTilt;
        Vector3 bloomNormal = {-tiltX, 1.0f, -tiltZ};
        Vector3 right = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
        Vector3 forward = {-sinf(stemAngle), 0.0f, cosf(stemAngle)};
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float radius = flower->bloomRadius * petalScale * 1.08f * 1.12f;
        float ox0 = (-right.x - forward.x) * radius;
        float oz0 = (-right.z - forward.z) * radius;
        float ox1 = ( right.x - forward.x) * radius;
        float oz1 = ( right.z - forward.z) * radius;
        float ox2 = ( right.x + forward.x) * radius;
        float oz2 = ( right.z + forward.z) * radius;
        float ox3 = (-right.x + forward.x) * radius;
        float oz3 = (-right.z + forward.z) * radius;
        Vector3 p0 = {head.x + ox0, head.y + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
        Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
        Vector3 p2 = {head.x + ox2, head.y + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
        Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
        if (texturedBloom) {
            int variantCount = atlasColumns * atlasRows;
            int variant = flower->bloomVariant % variantCount;
            int column = variant % atlasColumns;
            int row = variant / atlasColumns;
            float insetU = 0.008f / atlasColumns;
            float insetV = 0.008f / atlasRows;
            Vector4 uvRect = {
                (float)column / atlasColumns + insetU,
                (float)row / atlasRows + insetV,
                (float)(column + 1) / atlasColumns - insetU,
                (float)(row + 1) / atlasRows - insetV,
            };
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);
        } else {
            Nature_AddQuad(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                           phase, 1.0f, 1.0f, flower->petalColor, flower->petalColor);
        }
    }
    return Nature_ModelFromMesh(mesh, Nature_GetShader(texturedBloom));
}

static inline int Nature_FlowerBloomVertexCount(int variant, bool textured)
{
    if (textured) return 36; // 3 textured bloom cards (18) + 3D pollen disc dome (18) = 36
    int morph = variant % 4;
    switch (morph) {
        case 0: return 210; // 2-layer Rose/Poppy: 5 outer + 3 inner (192) + dome (18) = 210
        case 1: return 210; // 8 oval daisy petals (192) + phyllotaxis disc (18) = 210
        case 2: return 210; // Lavender spike: 4 whorls x 2 florets (192) + tip bud dome (18) = 210
        case 3: return 162; // 6 reflexed lanceolate petals (144) + center dome (18) = 162
        default: return 210;
    }
}

typedef enum {
    PETAL_SHAPE_OVAL,       // w(t) = W_max * sin(pi*t)^n
    PETAL_SHAPE_LANCEOLATE, // asymmetric: rapid bulge, long taper to sharp tip
    PETAL_SHAPE_OBCORDATE,  // heart-shaped with notched apex: w_base(t) - notch
    PETAL_SHAPE_SPATULATE,  // spoon-shaped: slender base, rounded wide tip
} PetalShapeType;

typedef struct {
    PetalShapeType shape;
    float wMax;
    float length;
    float exponent;     // n for oval/obcordate power
    float tPeak;        // peak location (e.g. 0.30 for lanceolate, 0.75 for spatulate)
    float notchDepth;   // notch depth for obcordate apex
    float curveAmount;  // vertical cupping / reflex (dy = curveAmount * t^2.5)
    float twistAmount;  // twist angle around petal axis (radians)
} PetalParams;

static inline float Nature_EvalPetalWidth(float t, const PetalParams *p)
{
    if (t <= 0.0f) return 0.002f;
    if (t >= 1.0f) return 0.000f;

    switch (p->shape) {
        case PETAL_SHAPE_OVAL: {
            // Chamomile / Daisy oval petal: wide body, soft smoothly rounded apex
            // Base at t=0 narrows to receptacle; mid reaches max width; smoothly rounds to zero at apex
            float s = sinf(PI * powf(t, 0.55f));
            if (s < 0.0f) s = 0.0f;
            float n = p->exponent > 0.0f ? p->exponent : 0.60f;
            float baseTaper = fminf(1.0f, t / 0.15f);
            return p->wMax * powf(s, n) * baseTaper;
        }
        case PETAL_SHAPE_LANCEOLATE: {
            float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.30f;
            if (t < tPeak) {
                return p->wMax * powf(t / tPeak, 0.50f);
            } else {
                return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.80f);
            }
        }
        case PETAL_SHAPE_OBCORDATE: {
            float s = sinf(PI * t);
            if (s < 0.0f) s = 0.0f;
            float wBase = p->wMax * powf(s, 1.20f);
            float notch = 0.0f;
            if (t > 0.80f) {
                float dt = (t - 1.0f) / 0.08f;
                float depth = p->notchDepth > 0.0f ? p->notchDepth : p->wMax * 0.40f;
                notch = depth * expf(-0.5f * dt * dt);
            }
            return fmaxf(0.003f, wBase - notch);
        }
        case PETAL_SHAPE_SPATULATE: {
            float tPeak = p->tPeak > 0.05f ? p->tPeak : 0.75f;
            if (t < tPeak) {
                return p->wMax * powf(t / tPeak, 0.65f);
            } else {
                return p->wMax * powf((1.0f - t) / (1.0f - tPeak), 1.40f);
            }
        }
        default:
            return p->wMax * sinf(PI * t);
    }
}

static inline float Nature_EvalPetalElevation(float t, const PetalParams *p)
{
    // Natural botanical 3D sigmoid cup curve: rises from receptacle with initial bowl slope
    float s = sinf(t * 1.5707963f);
    return p->curveAmount * (s * 1.25f - 0.25f * t * t);
}

static void Nature_AddParametricPetal(Mesh *mesh, int *cursor,
                                     Vector3 head, Vector3 dir, Vector3 side,
                                     const PetalParams *p, float phase,
                                     Color cBase, Color cMid, Color cTip)
{
    float len = p->length;
    float t0 = 0.05f;
    float t1 = 0.42f;
    float t2 = 0.70f;
    float t3 = 0.88f;

    float w0 = Nature_EvalPetalWidth(t0, p);
    float dy0 = Nature_EvalPetalElevation(t0, p);

    float w1 = Nature_EvalPetalWidth(t1, p);
    float dy1 = Nature_EvalPetalElevation(t1, p);

    float w2 = Nature_EvalPetalWidth(t2, p);
    float dy2 = Nature_EvalPetalElevation(t2, p);

    float w3 = Nature_EvalPetalWidth(t3, p);
    float dy3 = Nature_EvalPetalElevation(t3, p);

    float dy4 = Nature_EvalPetalElevation(1.0f, p);

    Vector3 side0 = side;
    Vector3 side1 = side;
    Vector3 side2 = side;
    Vector3 side3 = side;
    if (fabsf(p->twistAmount) > 0.001f) {
        float phi1 = p->twistAmount * t1;
        float phi2 = p->twistAmount * t2;
        float phi3 = p->twistAmount * t3;
        side1 = (Vector3){side.x * cosf(phi1), sinf(phi1) * len * 0.10f, side.z * cosf(phi1)};
        side2 = (Vector3){side.x * cosf(phi2), sinf(phi2) * len * 0.15f, side.z * cosf(phi2)};
        side3 = (Vector3){side.x * cosf(phi3), sinf(phi3) * len * 0.18f, side.z * cosf(phi3)};
    }

    Vector3 p0 = {head.x + dir.x * (len * t0), head.y + dy0 + 0.003f, head.z + dir.z * (len * t0)};
    Vector3 p1 = {head.x + dir.x * (len * t1), head.y + dy1,          head.z + dir.z * (len * t1)};
    Vector3 p2 = {head.x + dir.x * (len * t2), head.y + dy2,          head.z + dir.z * (len * t2)};
    Vector3 p3 = {head.x + dir.x * (len * t3), head.y + dy3,          head.z + dir.z * (len * t3)};
    Vector3 pApex = {head.x + dir.x * len,     head.y + dy4,          head.z + dir.z * len};

    // Transverse cupping: petal margins dish upward across width creating rich 3D volume
    float cup0 = w0 * 0.22f;
    float cup1 = w1 * 0.28f;
    float cup2 = w2 * 0.22f;
    float cup3 = w3 * 0.14f;

    Vector3 b0 = {p0.x - side0.x * w0, p0.y + cup0, p0.z - side0.z * w0};
    Vector3 b1 = {p0.x + side0.x * w0, p0.y + cup0, p0.z + side0.z * w0};

    Vector3 m0 = {p1.x - side1.x * w1, p1.y + cup1, p1.z - side1.z * w1};
    Vector3 m1 = {p1.x + side1.x * w1, p1.y + cup1, p1.z + side1.z * w1};

    Vector3 s0 = {p2.x - side2.x * w2, p2.y + cup2, p2.z - side2.z * w2};
    Vector3 s1 = {p2.x + side2.x * w2, p2.y + cup2, p2.z + side2.z * w2};

    Vector3 c0 = {p3.x - side3.x * w3, p3.y + cup3, p3.z - side3.z * w3};
    Vector3 c1 = {p3.x + side3.x * w3, p3.y + cup3, p3.z + side3.z * w3};

    // Cupped analytical normals across width
    Vector3 nL0 = Vector3Normalize((Vector3){dir.x * 0.15f + side.x * 0.32f, 0.92f, dir.z * 0.15f + side.z * 0.32f});
    Vector3 nR0 = Vector3Normalize((Vector3){dir.x * 0.15f - side.x * 0.32f, 0.92f, dir.z * 0.15f - side.z * 0.32f});
    Vector3 nL1 = Vector3Normalize((Vector3){dir.x * 0.32f + side1.x * 0.38f, 0.86f, dir.z * 0.32f + side1.z * 0.38f});
    Vector3 nR1 = Vector3Normalize((Vector3){dir.x * 0.32f - side1.x * 0.38f, 0.86f, dir.z * 0.32f - side1.z * 0.38f});
    Vector3 nL2 = Vector3Normalize((Vector3){dir.x * 0.52f + side2.x * 0.34f, 0.80f, dir.z * 0.52f + side2.z * 0.34f});
    Vector3 nR2 = Vector3Normalize((Vector3){dir.x * 0.52f - side2.x * 0.34f, 0.80f, dir.z * 0.52f - side2.z * 0.34f});
    Vector3 n3  = Vector3Normalize((Vector3){dir.x * 0.68f, 0.70f, dir.z * 0.68f});

    // Segment 0: Base to Mid (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, b1, nR0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, b0, nL0, phase, 0.85f, cBase);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);

    // Segment 1: Mid to Shoulder (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m1, nR1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, m0, nL1, phase, 0.95f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);

    // Segment 2: Shoulder to Rounding Shoulders (6 vertices)
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, s1, nR2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, c1, n3,  phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, s0, nL2, phase, 0.98f, cMid);
    Nature_SetVertex(mesh, (*cursor)++, c1, n3,  phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c0, n3,  phase, 1.00f, cTip);

    // Segment 3: Botanical Rounded Dome Apex (6 vertices, 2 fan triangles to pApex)
    Vector3 nTipL = Vector3Normalize((Vector3){dir.x * 0.70f - side.x * 0.35f, 0.60f, dir.z * 0.70f - side.z * 0.35f});
    Vector3 nTipR = Vector3Normalize((Vector3){dir.x * 0.70f + side.x * 0.35f, 0.60f, dir.z * 0.70f + side.z * 0.35f});
    Nature_SetVertex(mesh, (*cursor)++, c0,    nTipL, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, p3,    n3,    phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipL, phase, 1.00f, cTip);

    Nature_SetVertex(mesh, (*cursor)++, p3,    n3,    phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, c1,    nTipR, phase, 1.00f, cTip);
    Nature_SetVertex(mesh, (*cursor)++, pApex, nTipR, phase, 1.00f, cTip);
}

static void Nature_AddFlowerCenterDome(Mesh *mesh, int *cursor, Vector3 head, float cR, float cHeight, float phase, Color centerColor)
{
    Vector3 apex = {head.x, head.y + cHeight * 1.35f, head.z};
    Color apexColor = Nature_ScaleColor(centerColor, 1.25f);
    Color rimColor = Nature_ScaleColor(centerColor, 0.75f);
    // 6-sided dome with golden angle phyllotaxis micro-modulation
    for (int s = 0; s < 6; s++) {
        float a0 = (float)s * 1.04719755f;
        float a1 = (float)(s + 1) * 1.04719755f;
        float goldMod0 = 1.0f + 0.06f * sinf((float)s * 2.39996f);
        float goldMod1 = 1.0f + 0.06f * sinf((float)(s + 1) * 2.39996f);
        Vector3 r0 = {head.x + cosf(a0) * cR * goldMod0, head.y + 0.005f, head.z + sinf(a0) * cR * goldMod0};
        Vector3 r1 = {head.x + cosf(a1) * cR * goldMod1, head.y + 0.005f, head.z + sinf(a1) * cR * goldMod1};
        Vector3 norm = Vector3Normalize((Vector3){cosf((a0 + a1) * 0.5f) * 0.55f, 0.84f, sinf((a0 + a1) * 0.5f) * 0.55f});
        Nature_SetVertex(mesh, (*cursor)++, apex, (Vector3){0.0f, 1.0f, 0.0f}, phase, 1.0f, apexColor);
        Nature_SetVertex(mesh, (*cursor)++, r0, norm, phase, 0.95f, rimColor);
        Nature_SetVertex(mesh, (*cursor)++, r1, norm, phase, 0.95f, rimColor);
    }
}

MapFlowerField MapProp_CreateFlowerField(const MapFlowerPlacement *placements, int count,
                                         Color stemColor, Color centerColor,
                                         const char *petalTexturePath, float alphaCutoff,
                                         int atlasColumns, int atlasRows)
{
    MapFlowerField field = {0};
    if (!placements || count <= 0) return field;
    Vector3 boundsMin = placements[0].position;
    Vector3 boundsMax = placements[0].position;
    boundsMin.x -= placements[0].bloomRadius * 1.5f;
    boundsMin.z -= placements[0].bloomRadius * 1.5f;
    boundsMax.x += placements[0].bloomRadius * 1.5f;
    boundsMax.y += placements[0].height + placements[0].bloomRadius * 1.5f;
    boundsMax.z += placements[0].bloomRadius * 1.5f;
    if (GfxQuality_Get() >= GFX_HIGH)
        (void)NatureShadow_GetShader();
    field.textured = (petalTexturePath != NULL);
    if (atlasColumns < 1) atlasColumns = 1;
    if (atlasRows < 1) atlasRows = 1;

    // Calculate exact vertex count: 18 (basal rosette) + 24 (curved stem) + 12 (mid leaves) + bloom
    int vertexCount = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float extent = flower->bloomRadius * 1.6f;
        boundsMin.x = fminf(boundsMin.x, flower->position.x - extent);
        boundsMin.y = fminf(boundsMin.y, flower->position.y);
        boundsMin.z = fminf(boundsMin.z, flower->position.z - extent);
        boundsMax.x = fmaxf(boundsMax.x, flower->position.x + extent);
        boundsMax.y = fmaxf(boundsMax.y, flower->position.y + flower->height + extent);
        boundsMax.z = fmaxf(boundsMax.z, flower->position.z + extent);
        vertexCount += 84 + Nature_FlowerBloomVertexCount(flower->bloomVariant, field.textured);
    }
    Mesh mesh = Nature_AllocMesh(vertexCount);
    int cursor = 0;
    for (int i = 0; i < count; i++) {
        const MapFlowerPlacement *flower = &placements[i];
        float phase = flower->phase;
        float h = flower->height;
        Vector3 base = flower->position;
        float petalScale = flower->petalLengthScale > 0.0f ? flower->petalLengthScale : 1.0f;
        float r = flower->bloomRadius * petalScale;

        // 1. Basal leaf rosette: 4 spreading leaves hugging soil to anchor plant (24 verts)
        for (int b = 0; b < 4; b++) {
            float bAngle = flower->rotationDeg * DEG2RAD + (float)b * 1.570796f + 0.35f;
            Vector3 bDir = {cosf(bAngle), 0.0f, sinf(bAngle)};
            Vector3 bSide = {-bDir.z * 0.020f, 0.0f, bDir.x * 0.020f};
            float bLen = fmaxf(0.075f, flower->bloomRadius * 1.10f);
            Vector3 r0 = {base.x - bSide.x, base.y + 0.002f, base.z - bSide.z};
            Vector3 r1 = {base.x + bSide.x, base.y + 0.002f, base.z + bSide.z};
            Vector3 r2 = {base.x + bDir.x * bLen + bSide.x * 0.25f, base.y + 0.008f, base.z + bDir.z * bLen + bSide.z * 0.25f};
            Vector3 r3 = {base.x + bDir.x * bLen - bSide.x * 0.25f, base.y + 0.008f, base.z + bDir.z * bLen - bSide.z * 0.25f};
            Color basalColor = {46, 76, 32, 255};
            Nature_AddQuad(&mesh, &cursor, r0, r1, r2, r3, (Vector3){0.0f, 1.0f, 0.0f},
                           phase, 0.0f, 0.25f, basalColor, Nature_ScaleColor(basalColor, 1.25f));
        }

        // 2. Curved stem using quadratic Bezier (bowing organically under wind/gravity, 24 verts)
        float stemWidth = fmaxf(0.005f, flower->bloomRadius * 0.08f);
        float leanAngle = flower->rotationDeg * DEG2RAD * 0.73f + phase * 4.1f;
        float leanDist = h * (0.06f + 0.08f * (0.5f + 0.5f * sinf((float)i * 2.37f)));
        Vector3 stemP0 = base;
        Vector3 stemP1 = {base.x + cosf(leanAngle) * leanDist * 0.40f,
                          base.y + h * 0.52f,
                          base.z + sinf(leanAngle) * leanDist * 0.40f};
        Vector3 head = {base.x + cosf(leanAngle) * leanDist, base.y + h,
                        base.z + sinf(leanAngle) * leanDist};

        // Mid point on Bezier curve
        Vector3 midStem = {
            0.25f * stemP0.x + 0.50f * stemP1.x + 0.25f * head.x,
            0.25f * stemP0.y + 0.50f * stemP1.y + 0.25f * head.y,
            0.25f * stemP0.z + 0.50f * stemP1.z + 0.25f * head.z
        };

        for (int cross = 0; cross < 2; cross++) {
            float angle = flower->rotationDeg * DEG2RAD + cross * PI * 0.5f;
            Vector3 side = {cosf(angle) * stemWidth, 0.0f, sinf(angle) * stemWidth};
            Vector3 normal = {-sinf(angle), 0.08f, cosf(angle)};

            // Segment 0: base to mid
            Vector3 s0_0 = Vector3Subtract(stemP0, side);
            Vector3 s0_1 = Vector3Add(stemP0, side);
            Vector3 s0_2 = Vector3Add(midStem, Vector3Scale(side, 0.8f));
            Vector3 s0_3 = Vector3Subtract(midStem, Vector3Scale(side, 0.8f));
            Nature_AddQuad(&mesh, &cursor, s0_0, s0_1, s0_2, s0_3, normal, phase, 0.0f, 0.5f, stemColor, stemColor);

            // Segment 1: mid to head
            Vector3 s1_0 = s0_3;
            Vector3 s1_1 = s0_2;
            Vector3 s1_2 = Vector3Add(head, Vector3Scale(side, 0.5f));
            Vector3 s1_3 = Vector3Subtract(head, Vector3Scale(side, 0.5f));
            Nature_AddQuad(&mesh, &cursor, s1_0, s1_1, s1_2, s1_3, normal, phase, 0.5f, 1.0f, stemColor, stemColor);
        }

        // 3. Mid-stem foliage leaves (12 verts)
        for (int leaf = 0; leaf < 2; leaf++) {
            float rootT = leaf == 0 ? 0.35f : 0.65f;
            float leafAngle = leanAngle + (leaf == 0 ? 1.40f : -1.40f);
            Vector3 lDir = {cosf(leafAngle), 0.0f, sinf(leafAngle)};
            Vector3 lSide = {-lDir.z * 0.012f, 0.0f, lDir.x * 0.012f};
            float lLen = fmaxf(0.045f, flower->bloomRadius * 0.75f);
            float omt = 1.0f - rootT;
            Vector3 lRoot = {
                omt*omt*stemP0.x + 2*omt*rootT*stemP1.x + rootT*rootT*head.x,
                omt*omt*stemP0.y + 2*omt*rootT*stemP1.y + rootT*rootT*head.y,
                omt*omt*stemP0.z + 2*omt*rootT*stemP1.z + rootT*rootT*head.z
            };
            Vector3 lTip = {lRoot.x + lDir.x * lLen, lRoot.y + lLen * 0.35f, lRoot.z + lDir.z * lLen};
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(lRoot, lSide), Vector3Add(lRoot, lSide),
                           Vector3Add(lTip, Vector3Scale(lSide, 0.2f)), Vector3Subtract(lTip, Vector3Scale(lSide, 0.2f)),
                           (Vector3){0, 1, 0}, phase, rootT, rootT + 0.3f, stemColor, Nature_ScaleColor(stemColor, 1.15f));
        }

        // 4. Calyx cup: 4 green sepals cradling bloom head from below (24 verts)
        float calyxLen = r * 0.45f;
        for (int c = 0; c < 4; c++) {
            float cAngle = flower->rotationDeg * DEG2RAD + (float)c * 1.570796f + 0.392699f;
            Vector3 cDir = {cosf(cAngle), 0.0f, sinf(cAngle)};
            Vector3 cSide = {-cDir.z * calyxLen * 0.30f, 0.0f, cDir.x * calyxLen * 0.30f};
            Vector3 cBasePt = {head.x, head.y - stemWidth * 0.6f, head.z};
            Vector3 cTipPt = {head.x + cDir.x * calyxLen, head.y - stemWidth * 0.15f, head.z + cDir.z * calyxLen};
            Color calyxColor = Nature_ScaleColor(stemColor, 0.88f);
            Nature_AddQuad(&mesh, &cursor,
                           Vector3Subtract(cBasePt, cSide), Vector3Add(cBasePt, cSide),
                           Vector3Add(cTipPt, Vector3Scale(cSide, 0.20f)), Vector3Subtract(cTipPt, Vector3Scale(cSide, 0.20f)),
                           (Vector3){0.0f, 1.0f, 0.0f}, phase, 0.90f, 0.98f, calyxColor, Nature_ScaleColor(calyxColor, 1.22f));
        }

        // 5. Assemble 3D Flower Head
        if (field.textured) {
            int variantCount = atlasColumns * atlasRows;
            int variant = flower->bloomVariant % variantCount;
            int column = variant % atlasColumns;
            int row = variant / atlasColumns;
            float insetU = 0.008f / (float)atlasColumns;
            float insetV = 0.008f / (float)atlasRows;
            Vector4 uvRect = {
                (float)column / (float)atlasColumns + insetU,
                (float)row / (float)atlasRows + insetV,
                (float)(column + 1) / (float)atlasColumns - insetU,
                (float)(row + 1) / (float)atlasRows - insetV,
            };

            float stemAngle = flower->rotationDeg * DEG2RAD;
            float headTilt = 0.22f + 0.25f * (0.5f + 0.5f * sinf((float)i * 1.91f + phase));
            float tiltX = cosf(leanAngle) * headTilt;
            float tiltZ = sinf(leanAngle) * headTilt;
            Vector3 bloomNormal = Vector3Normalize((Vector3){-tiltX, 1.0f, -tiltZ});
            Vector3 right = {cosf(stemAngle), 0.0f, sinf(stemAngle)};
            Vector3 forward = {-sinf(stemAngle), 0.0f, cosf(stemAngle)};

            // Card 0: Horizontal / upward-tilted face bloom disc
            float ox0 = (-right.x - forward.x) * r;
            float oz0 = (-right.z - forward.z) * r;
            float ox1 = ( right.x - forward.x) * r;
            float oz1 = ( right.z - forward.z) * r;
            float ox2 = ( right.x + forward.x) * r;
            float oz2 = ( right.z + forward.z) * r;
            float ox3 = (-right.x + forward.x) * r;
            float oz3 = (-right.z + forward.z) * r;
            Vector3 p0 = {head.x + ox0, head.y + tiltX * ox0 + tiltZ * oz0, head.z + oz0};
            Vector3 p1 = {head.x + ox1, head.y + tiltX * ox1 + tiltZ * oz1, head.z + oz1};
            Vector3 p2 = {head.x + ox2, head.y + tiltX * ox2 + tiltZ * oz2, head.z + oz2};
            Vector3 p3 = {head.x + ox3, head.y + tiltX * ox3 + tiltZ * oz3, head.z + oz3};
            Nature_AddTexturedBloom(&mesh, &cursor, p0, p1, p2, p3, bloomNormal,
                                    phase, flower->petalColor, uvRect);

            // Card 1: Angled cross card 1 (vertical profile rotated 45 deg)
            float a1 = stemAngle + 0.785398f;
            Vector3 r1 = {cosf(a1) * r, 0.0f, sinf(a1) * r};
            Vector3 f1 = {-sinf(a1) * r, 0.0f, cosf(a1) * r};
            float vH = r * 0.75f;
            Vector3 q0 = {head.x - r1.x, head.y - vH * 0.15f, head.z - r1.z};
            Vector3 q1 = {head.x + r1.x, head.y - vH * 0.15f, head.z + r1.z};
            Vector3 q2 = {head.x + r1.x, head.y + vH * 0.85f, head.z + r1.z};
            Vector3 q3 = {head.x - r1.x, head.y + vH * 0.85f, head.z - r1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, q0, q1, q2, q3, f1,
                                    phase, flower->petalColor, uvRect);

            // Card 2: Angled cross card 2 (orthogonal to Card 1)
            Vector3 k0 = {head.x - f1.x, head.y - vH * 0.15f, head.z - f1.z};
            Vector3 k1 = {head.x + f1.x, head.y - vH * 0.15f, head.z + f1.z};
            Vector3 k2 = {head.x + f1.x, head.y + vH * 0.85f, head.z + f1.z};
            Vector3 k3 = {head.x - f1.x, head.y + vH * 0.85f, head.z - f1.z};
            Nature_AddTexturedBloom(&mesh, &cursor, k0, k1, k2, k3, r1,
                                    phase, flower->petalColor, uvRect);

            // 3D Pollen center dome
            Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.22f, r * 0.10f, phase, centerColor);
        } else {
            int morph = flower->bloomVariant % 4;
            Color cBase = Nature_ScaleColor(flower->petalColor, 0.72f);
            Color cMid = flower->petalColor;
            Color cTip = Nature_ScaleColor(flower->petalColor, 1.15f);

            switch (morph) {
                case 0: { // Multi-layer Rose / Poppy: 5 outer (60) + 3 inner (36) + dome (18) = 114 verts
                    PetalParams pOuter = {
                        .shape = PETAL_SHAPE_OBCORDATE,
                        .wMax = r * 0.48f,
                        .length = r * 1.05f,
                        .exponent = 1.25f,
                        .notchDepth = r * 0.16f,
                        .curveAmount = r * 0.38f,
                        .twistAmount = 0.08f,
                    };
                    for (int k = 0; k < 5; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 1.256637f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pOuter,
                                                 phase, cBase, cMid, cTip);
                    }
                    PetalParams pInner = {
                        .shape = PETAL_SHAPE_OBCORDATE,
                        .wMax = r * 0.38f,
                        .length = r * 0.78f,
                        .exponent = 1.15f,
                        .notchDepth = r * 0.12f,
                        .curveAmount = r * 0.52f,
                        .twistAmount = -0.10f,
                    };
                    Color cInBase = Nature_ScaleColor(cBase, 0.90f);
                    Color cInMid = Nature_ScaleColor(cMid, 0.95f);
                    for (int k = 0; k < 3; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + 0.628318f + (float)k * 2.094395f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pInner,
                                                 phase, cInBase, cInMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.28f, r * 0.16f, phase, centerColor);
                    break;
                }
                case 1: { // 8-petal Daisy / Chrysanthemum: oval rounded petals (96 verts) + phyllotaxis dome (18 verts) = 114 verts
                    PetalParams pDaisy = {
                        .shape = PETAL_SHAPE_OVAL,
                        .wMax = r * 0.35f,
                        .length = r * 1.18f,
                        .exponent = 0.52f,
                        .curveAmount = r * 0.22f,
                        .twistAmount = 0.03f,
                    };
                    for (int k = 0; k < 8; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 0.785398f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pDaisy,
                                                 phase, cBase, cMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.35f, r * 0.15f, phase, centerColor);
                    break;
                }
                case 2: { // Lavender Spike Inflorescence: 4 whorls x 2 florets (96 verts) + tip bud (18 verts) = 114 verts
                    float spH = r * 2.2f;
                    float phaseSpiral = 0.38f;
                    for (int k = 0; k < 4; k++) {
                        float s = 0.22f + (float)k * 0.24f;
                        Vector3 tierHead = {head.x, head.y + spH * s, head.z};
                        float bloomFactor = (s < 0.65f) ? 1.0f : fmaxf(0.40f, 1.0f - (s - 0.65f) / 0.35f);
                        PetalParams pFloret = {
                            .shape = PETAL_SHAPE_OVAL,
                            .wMax = r * 0.22f * bloomFactor,
                            .length = r * 0.60f * bloomFactor,
                            .exponent = 1.10f,
                            .curveAmount = r * 0.25f * bloomFactor,
                            .twistAmount = 0.04f,
                        };
                        for (int j = 0; j < 2; j++) {
                            float angle = flower->rotationDeg * DEG2RAD + (float)k * phaseSpiral + (float)j * PI;
                            Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                            Vector3 side = {-dir.z, 0.0f, dir.x};
                            Nature_AddParametricPetal(&mesh, &cursor, tierHead, dir, side, &pFloret,
                                                     phase, cBase, cMid, cTip);
                        }
                    }
                    Vector3 tipApex = {head.x, head.y + spH, head.z};
                    Nature_AddFlowerCenterDome(&mesh, &cursor, tipApex, r * 0.16f, r * 0.18f, phase, centerColor);
                    break;
                }
                case 3: { // 6-petal Bell / Campanula / Star: lanceolate reflexed petals (72) + dome (18) = 90 verts
                    PetalParams pBell = {
                        .shape = PETAL_SHAPE_LANCEOLATE,
                        .wMax = r * 0.34f,
                        .length = r * 1.12f,
                        .tPeak = 0.28f,
                        .curveAmount = r * 0.36f,
                        .twistAmount = 0.06f,
                    };
                    for (int k = 0; k < 6; k++) {
                        float angle = flower->rotationDeg * DEG2RAD + (float)k * 1.04719755f;
                        Vector3 dir = {cosf(angle), 0.0f, sinf(angle)};
                        Vector3 side = {-dir.z, 0.0f, dir.x};
                        Nature_AddParametricPetal(&mesh, &cursor, head, dir, side, &pBell,
                                                 phase, cBase, cMid, cTip);
                    }
                    Nature_AddFlowerCenterDome(&mesh, &cursor, head, r * 0.24f, r * 0.14f, phase, centerColor);
                    break;
                }
            }
        }
    }
    field.textured = (petalTexturePath != NULL);
    field.model = Nature_ModelFromMesh(mesh, Nature_GetShader(field.textured));
    field.farModel = Nature_BuildFlowerFarModel(placements, count, field.textured,
                                                 atlasColumns, atlasRows);
    field.farReady = field.farModel.meshCount > 0;
    if (GfxQuality_Get() >= GFX_MED)
        field.shadowModel = Nature_BuildFlowerShadowModel(placements, count);
    field.shadowReady = field.shadowModel.meshCount > 0;
    field.alphaCutoff = alphaCutoff > 0.0f ? fminf(alphaCutoff, 0.9f) : 0.35f;
    field.boundsCenter = Vector3Scale(Vector3Add(boundsMin, boundsMax), 0.5f);
    field.boundsRadius = Vector3Distance(boundsMin, boundsMax) * 0.5f;
    field.drawDistance = 78.0f;
    field.lodDistance = 34.0f;
    field.shadowDistance = 26.0f;
    if (field.textured) {
        Texture2D petalTexture = ResourceManager_LoadTexture(petalTexturePath);
        GenTextureMipmaps(&petalTexture);
        SetTextureFilter(petalTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(petalTexture, TEXTURE_WRAP_CLAMP);
        field.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
        field.farModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = petalTexture;
    }
    field.ready = true;
    return field;
}

void MapProp_SetFlowerFieldDrawDistance(MapFlowerField *field, float drawDistance)
{
    if (!field) return;
    field->drawDistance = drawDistance;
}

void MapProp_SetFlowerFieldLod(MapFlowerField *field, float lodDistance, float shadowDistance)
{
    if (!field) return;
    field->lodDistance = lodDistance;
    field->shadowDistance = shadowDistance;
}

void MapProp_DrawFlowerField(MapFlowerField *field, Vector3 worldOffset, float time,
                             Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready) return;
    s_natureRenderStats.flowerFieldsTested++;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    if (!Nature_IsChunkVisible(center, field->boundsRadius)) {
        s_natureRenderStats.flowerFieldsFrustumCulled++;
        return;
    }
    GfxQuality quality = GfxQuality_Get();
    float rangeScale = quality >= GFX_HIGH ? 1.0f
                     : quality == GFX_MED ? 0.84f
                     : quality == GFX_LOW ? 0.68f : 0.55f;
    float dx = camera.position.x - center.x;
    float dz = camera.position.z - center.z;
    float centerDistance = sqrtf(dx * dx + dz * dz);
    float visibleDistance = centerDistance - field->boundsRadius;
    if (field->drawDistance > 0.0f &&
        visibleDistance > field->drawDistance * rangeScale) {
        s_natureRenderStats.flowerFieldsDistanceCulled++;
        return;
    }
    float lodScale = quality >= GFX_HIGH ? 1.0f
                   : quality == GFX_MED ? 0.82f
                   : quality == GFX_LOW ? 0.58f : 0.45f;
    if (field->farReady && field->lodDistance > 0.0f) {
        float focalDx = camera.position.x - camera.target.x;
        float focalDz = camera.position.z - camera.target.z;
        float focalDistH = sqrtf(focalDx * focalDx + focalDz * focalDz);
        float fovyRad = fmaxf(camera.fovy, 15.0f) * DEG2RAD;
        float zoomFactor = tanf(45.0f * 0.5f * DEG2RAD) / tanf(fovyRad * 0.5f);
        if (zoomFactor < 0.8f) zoomFactor = 0.8f;
        if (zoomFactor > 2.5f) zoomFactor = 2.5f;
        float spatialHash = sinf(field->boundsCenter.x * 12.9898f
                                 + field->boundsCenter.z * 78.233f);
        spatialHash -= floorf(spatialHash);
        float threshold = focalDistH + field->lodDistance * lodScale * zoomFactor + (spatialHash - 0.5f) * 3.0f;
        float hysteresis = quality >= GFX_HIGH ? 1.25f : 2.0f;
        if (field->farLod) {
            if (visibleDistance < threshold - hysteresis)
                field->farLod = false;
        } else if (visibleDistance > threshold + hysteresis) {
            field->farLod = true;
        }
    } else {
        field->farLod = false;
    }
    bool useFarModel = field->farReady && field->farLod;
    float shadowScale = quality >= GFX_HIGH ? 1.0f : 0.78f;
    bool shadowInRange = field->shadowDistance <= 0.0f ||
                         visibleDistance <= field->shadowDistance * shadowScale;
    NatureShadowMode shadowMode = Nature_GetShadowMode();
    bool realShadowActive = quality >= GFX_HIGH && EnvShadow_IsEnabled() &&
                            shadowMode != NATURE_SHADOW_PROJECTED_ONLY;
    bool useProjectedShadows = quality >= GFX_MED &&
                               shadowMode != NATURE_SHADOW_REAL_ONLY;
    if (field->shadowReady && useProjectedShadows && shadowInRange) {
        Shader shadowShader = FlowerShadow_GetShader();
        Nature_UpdateProjectedShadowShader(shadowShader, realShadowActive);
        rlDisableDepthMask();
        BeginBlendMode(BLEND_MULTIPLIED);
        DrawModel(field->shadowModel, worldOffset, 1.0f, WHITE);
        s_natureRenderStats.flowerShadowDraws++;
        EndBlendMode();
        rlEnableDepthMask();
    } else if (field->shadowReady && useProjectedShadows && !shadowInRange) {
        s_natureRenderStats.flowerShadowDistanceCulled++;
    }
    Shader shader = Nature_GetShader(field->textured);
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader(shader, time, windDirection, windStrength,
                        field->textured, field->alphaCutoff,
                        NATURE_WIND_RESPONSE_FLOWER);
    rlDisableBackfaceCulling();
    DrawModel(useFarModel ? field->farModel : field->model, worldOffset, 1.0f, WHITE);
    Nature_EndWindReceiverShader();
    s_natureRenderStats.flowerDraws++;
    if (useFarModel)
        s_natureRenderStats.flowerFarDraws++;
    else
        s_natureRenderStats.flowerNearDraws++;
    rlEnableBackfaceCulling();
}

void MapProp_DrawFlowerFieldShadowCaster(MapFlowerField *field, Vector3 worldOffset,
                                         float time, Vector2 windDirection, float windStrength)
{
    if (!field || !field->ready || GfxQuality_Get() < GFX_HIGH ||
        Nature_GetShadowMode() == NATURE_SHADOW_PROJECTED_ONLY ||
        !Nature_ShadowCasterTypeEnabled(true))
        return;
    Vector3 center = Vector3Add(field->boundsCenter, worldOffset);
    if (!Nature_IntersectsDynamicShadowCoverage(center, field->boundsRadius) &&
        !Nature_ShadowCasterFilterActive())
        return;

    Shader shader = NatureShadow_GetShader();
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader(shader, time, windDirection, windStrength,
                              field->textured, field->alphaCutoff,
                              NATURE_WIND_RESPONSE_FLOWER);
    // Use the same petals and stems as the visible near field. The far mesh
    // loses the small silhouettes that make flower shadows recognizable.
    Model castModel = field->model;
    Shader previous = castModel.materials[0].shader;
    castModel.materials[0].shader = shader;
    rlDisableBackfaceCulling();
    DrawModel(castModel, worldOffset, 1.0f, WHITE);
    Nature_EndWindReceiverShader();
    rlEnableBackfaceCulling();
    castModel.materials[0].shader = previous;
}

void MapProp_UnloadFlowerField(MapFlowerField *field)
{
    if (!field || !field->ready) return;
    if (field->shadowReady)
        UnloadModel(field->shadowModel);
    if (field->farReady)
        UnloadModel(field->farModel);
    UnloadModel(field->model);
    field->farReady = false;
    field->farLod = false;
    field->shadowReady = false;
    field->boundsRadius = 0.0f;
    field->ready = false;
}

static void Water_SetVertex(Mesh *mesh, int index, Vector3 p, Vector2 uv, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->texcoords[index * 2 + 0] = uv.x;
    mesh->texcoords[index * 2 + 1] = uv.y;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = 255;
}

static void Water_SetBedVertex(Mesh *mesh, int index, Vector3 p, Vector3 n, Vector2 uv, Color color)
{
    mesh->vertices[index * 3 + 0] = p.x;
    mesh->vertices[index * 3 + 1] = p.y;
    mesh->vertices[index * 3 + 2] = p.z;
    mesh->normals[index * 3 + 0] = n.x;
    mesh->normals[index * 3 + 1] = n.y;
    mesh->normals[index * 3 + 2] = n.z;
    mesh->texcoords[index * 2 + 0] = uv.x;
    mesh->texcoords[index * 2 + 1] = uv.y;
    mesh->colors[index * 4 + 0] = color.r;
    mesh->colors[index * 4 + 1] = color.g;
    mesh->colors[index * 4 + 2] = color.b;
    mesh->colors[index * 4 + 3] = color.a;
}

static float Water_EdgeScale(float angle, float radial, unsigned int seed)
{
    float seedPhase = (float)(seed & 1023u) * 0.0173f;
    float shorelineNoise = sinf(angle * 5.0f + seedPhase) * 0.024f
                         + sinf(angle * 11.0f - seedPhase * 0.63f) * 0.013f
                         + sinf(angle * 17.0f + 1.7f) * 0.006f;
    float edgeWeight = radial * radial;
    edgeWeight *= edgeWeight;
    return 1.0f + shorelineNoise * edgeWeight;
}

MapWaterConfig MapProp_DefaultWaterConfig(MapWaterEcosystem eco)
{
    MapWaterConfig c = {0};
    c.shape = WATER_SHAPE_RADIAL;
    c.ecosystem = eco;
    c.maxDepth = 1.25f;
    c.waveHeight = 0.038f;
    c.waveScale = 0.95f;
    c.waveSpeed = 0.72f;
    c.detailScale = 0.10f;
    c.detailStrength = 0.16f;
    c.causticsStrength = 0.65f;
    c.causticsScale = 1.20f;
    c.foamThreshold = 0.12f;
    c.refractionStrength = 0.04f;
    c.scatterCoeff = 0.45f;
    c.foamColor = (Color){215, 230, 222, 255};
    c.bankInnerColor = (Color){52, 58, 43, 255};
    c.bankOuterColor = (Color){65, 84, 51, 255};

    switch (eco) {
        case WATER_ECO_ALPINE_STREAM:
            c.absorption = (Vector3){0.80f, 0.20f, 0.05f};
            c.scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
            c.deepColor = (Color){10, 38, 42, 255};
            c.shallowColor = (Color){48, 92, 85, 255};
            c.causticsStrength = 0.72f;
            break;
        case WATER_ECO_FOREST_SWAMP:
            c.absorption = (Vector3){0.30f, 0.70f, 1.30f};
            c.scatterColor = (Vector3){0.80f, 0.55f, 0.22f};
            c.deepColor = (Color){28, 20, 12, 255};
            c.shallowColor = (Color){68, 52, 32, 255};
            c.causticsStrength = 0.35f;
            c.maxDepth = 0.65f;
            break;
        case WATER_ECO_TROPICAL_SHALLOW:
            c.absorption = (Vector3){0.45f, 0.15f, 0.08f};
            c.scatterColor = (Vector3){0.15f, 0.92f, 0.88f};
            c.deepColor = (Color){12, 52, 68, 255};
            c.shallowColor = (Color){58, 122, 118, 255};
            c.causticsStrength = 0.80f;
            break;
        case WATER_ECO_STAGNANT_POND:
            c.absorption = (Vector3){0.90f, 0.60f, 0.90f};
            c.scatterColor = (Vector3){0.42f, 0.68f, 0.28f};
            c.deepColor = (Color){20, 32, 16, 255};
            c.shallowColor = (Color){50, 72, 40, 255};
            c.causticsStrength = 0.40f;
            c.maxDepth = 0.55f;
            break;
        case WATER_ECO_CUSTOM:
        default:
            c.absorption = (Vector3){0.80f, 0.20f, 0.05f};
            c.scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
            c.deepColor = (Color){10, 38, 42, 255};
            c.shallowColor = (Color){48, 92, 85, 255};
            break;
    }
    return c;
}

static void Water_ApplyConfigDefaults(MapWaterConfig *config)
{
    if (config->maxDepth <= 0.0f) config->maxDepth = 1.25f;
    if (config->maxDepth > 1.30f) config->maxDepth = 1.30f;

    if (config->absorption.x <= 0.0f && config->absorption.y <= 0.0f && config->absorption.z <= 0.0f) {
        MapWaterConfig def = MapProp_DefaultWaterConfig(config->ecosystem);
        config->absorption = def.absorption;
        if (config->scatterColor.x <= 0.0f && config->scatterColor.y <= 0.0f) {
            config->scatterColor = def.scatterColor;
        }
        if (config->scatterCoeff <= 0.0f) config->scatterCoeff = def.scatterCoeff;
        if (config->deepColor.a == 0) config->deepColor = def.deepColor;
        if (config->shallowColor.a == 0) config->shallowColor = def.shallowColor;
        if (config->foamColor.a == 0) config->foamColor = def.foamColor;
        if (config->causticsStrength <= 0.0f) config->causticsStrength = def.causticsStrength;
        if (config->causticsScale <= 0.0f) config->causticsScale = def.causticsScale;
    }
    if (config->scatterColor.x <= 0.0f && config->scatterColor.y <= 0.0f && config->scatterColor.z <= 0.0f) {
        config->scatterColor = (Vector3){0.20f, 0.85f, 0.72f};
    }
    if (config->scatterCoeff <= 0.0f) config->scatterCoeff = 0.45f;
    if (config->causticsStrength < 0.0f) config->causticsStrength = 0.0f;
    if (config->causticsStrength == 0.0f) config->causticsStrength = 0.65f;
    if (config->causticsScale <= 0.0f) config->causticsScale = 1.20f;
    if (config->foamThreshold <= 0.0f) config->foamThreshold = 0.12f;
    if (config->waveHeight <= 0.0f) config->waveHeight = 0.038f;
    if (config->waveScale <= 0.0f) config->waveScale = 0.95f;
    if (config->waveSpeed <= 0.0f) config->waveSpeed = 0.72f;
    if (config->detailScale <= 0.0f) config->detailScale = 0.10f;
    if (config->detailStrength <= 0.0f) config->detailStrength = 0.16f;
}

MapWaterSurface MapProp_CreateWaterSurface(MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (config.radiusX <= 0.0f || config.radiusZ <= 0.0f) return water;
    if (config.segments < 24) config.segments = 24;
    if (config.segments > 192) config.segments = 192;
    if (config.rings < 2) config.rings = 2;
    if (config.rings > 32) config.rings = 32;

    Water_ApplyConfigDefaults(&config);
    config.shape = WATER_SHAPE_RADIAL;
    water.config = config;

    Mesh lakeMesh = {0};
    int lakeVertices = config.segments * config.rings * 6;
    lakeMesh.vertexCount = lakeVertices;
    lakeMesh.triangleCount = lakeVertices / 3;
    lakeMesh.vertices = MemAlloc((unsigned int)lakeVertices * 3u * sizeof(float));
    lakeMesh.texcoords = MemAlloc((unsigned int)lakeVertices * 2u * sizeof(float));
    lakeMesh.colors = MemAlloc((unsigned int)lakeVertices * 4u * sizeof(unsigned char));
    int cursor = 0;
    for (int ring = 0; ring < config.rings; ring++) {
        float r0 = (float)ring / config.rings;
        float r1 = (float)(ring + 1) / config.rings;
        for (int segment = 0; segment < config.segments; segment++) {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float e00 = Water_EdgeScale(a0, r0, config.seed);
            float e01 = Water_EdgeScale(a1, r0, config.seed);
            float e10 = Water_EdgeScale(a0, r1, config.seed);
            float e11 = Water_EdgeScale(a1, r1, config.seed);
            Vector3 p00 = {cosf(a0) * config.radiusX * r0 * e00, 0.0f, sinf(a0) * config.radiusZ * r0 * e00};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0 * e01, 0.0f, sinf(a1) * config.radiusZ * r0 * e01};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1 * e10, 0.0f, sinf(a0) * config.radiusZ * r1 * e10};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1 * e11, 0.0f, sinf(a1) * config.radiusZ * r1 * e11};
            Vector2 uv00 = {cosf(a0) * r0 * 0.5f + 0.5f, sinf(a0) * r0 * 0.5f + 0.5f};
            Vector2 uv01 = {cosf(a1) * r0 * 0.5f + 0.5f, sinf(a1) * r0 * 0.5f + 0.5f};
            Vector2 uv10 = {cosf(a0) * r1 * 0.5f + 0.5f, sinf(a0) * r1 * 0.5f + 0.5f};
            Vector2 uv11 = {cosf(a1) * r1 * 0.5f + 0.5f, sinf(a1) * r1 * 0.5f + 0.5f};
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&lakeMesh, cursor++, p10, uv10, WHITE);
        }
    }
    water.waterModel = Nature_ModelFromMesh(lakeMesh, Water_GetShader());
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    // ── Build 3D Concave Lake Bed (Nền hồ 3D thật) ─────────────────────────
    Mesh bedMesh = {0};
    int bedVertices = config.segments * config.rings * 6;
    bedMesh.vertexCount = bedVertices;
    bedMesh.triangleCount = bedVertices / 3;
    bedMesh.vertices = MemAlloc((unsigned int)bedVertices * 3u * sizeof(float));
    bedMesh.normals = MemAlloc((unsigned int)bedVertices * 3u * sizeof(float));
    bedMesh.texcoords = MemAlloc((unsigned int)bedVertices * 2u * sizeof(float));
    bedMesh.colors = MemAlloc((unsigned int)bedVertices * 4u * sizeof(unsigned char));
    int bedCursor = 0;
    for (int ring = 0; ring < config.rings; ring++) {
        float r0 = (float)ring / config.rings;
        float r1 = (float)(ring + 1) / config.rings;
        float d0 = config.maxDepth * (1.0f - powf(r0, 1.6f)) + 0.015f;
        float d1 = config.maxDepth * (1.0f - powf(r1, 1.6f)) + 0.015f;
        float y0 = -d0;
        float y1 = -d1;
        Color c0 = Nature_LerpColor((Color){135, 128, 110, 255}, (Color){195, 185, 165, 255}, r0);
        Color c1 = Nature_LerpColor((Color){135, 128, 110, 255}, (Color){195, 185, 165, 255}, r1);
        for (int segment = 0; segment < config.segments; segment++) {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float e00 = Water_EdgeScale(a0, r0, config.seed);
            float e01 = Water_EdgeScale(a1, r0, config.seed);
            float e10 = Water_EdgeScale(a0, r1, config.seed);
            float e11 = Water_EdgeScale(a1, r1, config.seed);
            Vector3 p00 = {cosf(a0) * config.radiusX * r0 * e00, y0, sinf(a0) * config.radiusZ * r0 * e00};
            Vector3 p01 = {cosf(a1) * config.radiusX * r0 * e01, y0, sinf(a1) * config.radiusZ * r0 * e01};
            Vector3 p10 = {cosf(a0) * config.radiusX * r1 * e10, y1, sinf(a0) * config.radiusZ * r1 * e10};
            Vector3 p11 = {cosf(a1) * config.radiusX * r1 * e11, y1, sinf(a1) * config.radiusZ * r1 * e11};
            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};

            float slope0 = 1.6f * powf(fmaxf(0.01f, r0), 0.6f) * (config.maxDepth / fmaxf(config.radiusX, config.radiusZ));
            float slope1 = 1.6f * powf(fmaxf(0.01f, r1), 0.6f) * (config.maxDepth / fmaxf(config.radiusX, config.radiusZ));
            Vector3 n00 = Vector3Normalize((Vector3){cosf(a0) * slope0, 1.0f, sinf(a0) * slope0});
            Vector3 n01 = Vector3Normalize((Vector3){cosf(a1) * slope0, 1.0f, sinf(a1) * slope0});
            Vector3 n10 = Vector3Normalize((Vector3){cosf(a0) * slope1, 1.0f, sinf(a0) * slope1});
            Vector3 n11 = Vector3Normalize((Vector3){cosf(a1) * slope1, 1.0f, sinf(a1) * slope1});

            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p01, n01, uv01, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, c1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p00, n00, uv00, c0);
            Water_SetBedVertex(&bedMesh, bedCursor++, p11, n11, uv11, c1);
            Water_SetBedVertex(&bedMesh, bedCursor++, p10, n10, uv10, c1);
        }
    }
    water.bedModel = Nature_ModelFromMesh(bedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/stone_path_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    const int bankRings = 3;
    Mesh bankMesh = Nature_AllocMesh(config.segments * bankRings * 6);
    cursor = 0;
    float bankOuterY = config.bankGroundY - config.center.y;
    for (int ring = 0; ring < bankRings; ring++) {
        float t0 = (float)ring / bankRings;
        float t1 = (float)(ring + 1) / bankRings;
        float s0 = t0 * t0 * (3.0f - 2.0f * t0);
        float s1 = t1 * t1 * (3.0f - 2.0f * t1);
        Color c0 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s0);
        Color c1 = Nature_LerpColor(config.bankInnerColor, config.bankOuterColor, s1);
        for (int segment = 0; segment < config.segments; segment++) {
            float a0 = (float)segment * 2.0f * PI / config.segments;
            float a1 = (float)(segment + 1) * 2.0f * PI / config.segments;
            float inner0 = Water_EdgeScale(a0, 1.0f, config.seed);
            float inner1 = Water_EdgeScale(a1, 1.0f, config.seed);
            float seedPhase = (float)(config.seed & 1023u) * 0.0173f;
            float habitat0 = 0.5f + 0.34f * sinf(a0 * 4.0f + seedPhase)
                                   + 0.16f * sinf(a0 * 9.0f - seedPhase * 0.61f);
            float habitat1 = 0.5f + 0.34f * sinf(a1 * 4.0f + seedPhase)
                                   + 0.16f * sinf(a1 * 9.0f - seedPhase * 0.61f);
            habitat0 = fmaxf(0.0f, fminf(1.0f, habitat0));
            habitat1 = fmaxf(0.0f, fminf(1.0f, habitat1));
            float width0 = config.bankWidth * (0.10f + 1.04f * habitat0 * habitat0);
            float width1 = config.bankWidth * (0.10f + 1.04f * habitat1 * habitat1);
            float rx00 = config.radiusX * 0.985f * inner0 + width0 * s0;
            float rz00 = config.radiusZ * 0.985f * inner0 + width0 * 0.72f * s0;
            float rx01 = config.radiusX * 0.985f * inner1 + width1 * s0;
            float rz01 = config.radiusZ * 0.985f * inner1 + width1 * 0.72f * s0;
            float rx10 = config.radiusX * 0.985f * inner0 + width0 * s1;
            float rz10 = config.radiusZ * 0.985f * inner0 + width0 * 0.72f * s1;
            float rx11 = config.radiusX * 0.985f * inner1 + width1 * s1;
            float rz11 = config.radiusZ * 0.985f * inner1 + width1 * 0.72f * s1;
            float y0 = -0.018f + (bankOuterY + 0.018f) * s0;
            float y1 = -0.018f + (bankOuterY + 0.018f) * s1;
            Vector3 p00 = {cosf(a0) * rx00, y0, sinf(a0) * rz00};
            Vector3 p01 = {cosf(a1) * rx01, y0, sinf(a1) * rz01};
            Vector3 p10 = {cosf(a0) * rx10, y1, sinf(a0) * rz10};
            Vector3 p11 = {cosf(a1) * rx11, y1, sinf(a1) * rz11};
            float shade0 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a0 * 7.0f - seedPhase * 0.7f));
            float shade1 = 0.88f + 0.16f * (0.5f + 0.5f * sinf(a1 * 7.0f - seedPhase * 0.7f));
            Nature_AddQuad4(&bankMesh, &cursor, p00, p01, p11, p10,
                            (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
                            Nature_ScaleColor(c0, shade0), Nature_ScaleColor(c0, shade1),
                            Nature_ScaleColor(c1, shade1), Nature_ScaleColor(c1, shade0));
        }
    }
    water.bankModel = Nature_ModelFromMesh(bankMesh, Nature_GetShader(false));
    const int cellCount = WATER_FIELD_SIZE * WATER_FIELD_SIZE;
    water.waveHeightField = calloc((size_t)cellCount, sizeof(float));
    water.waveNextField = calloc((size_t)cellCount, sizeof(float));
    water.waveVelocityField = calloc((size_t)cellCount, sizeof(float));
    water.wavePixels = malloc((size_t)cellCount * 4u);
    if (water.waveHeightField && water.waveNextField && water.waveVelocityField && water.wavePixels) {
        for (int i = 0; i < cellCount; i++) {
            water.wavePixels[i * 4 + 0] = 128;
            water.wavePixels[i * 4 + 1] = 128;
            water.wavePixels[i * 4 + 2] = 128;
            water.wavePixels[i * 4 + 3] = 128;
        }
        Image waveImage = {
            .data = water.wavePixels,
            .width = WATER_FIELD_SIZE, .height = WATER_FIELD_SIZE,
            .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
        };
        water.waveFieldTex = LoadTextureFromImage(waveImage);
        if (water.waveFieldTex.id > 0) {
            SetTextureFilter(water.waveFieldTex, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(water.waveFieldTex, TEXTURE_WRAP_CLAMP);
        }
    }
    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateWaterPatch(Vector3 center, float width, float depth, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    if (width <= 0.0f || depth <= 0.0f) return water;

    Water_ApplyConfigDefaults(&config);
    config.center = center;
    config.radiusX = width * 0.5f;
    config.radiusZ = depth * 0.5f;
    config.shape = WATER_SHAPE_RECT;
    water.config = config;

    int segX = config.segments >= 8 ? config.segments : 16;
    int segZ = config.rings >= 8 ? config.rings : 16;
    int totalVerts = segX * segZ * 6;

    Mesh patchMesh = {0};
    patchMesh.vertexCount = totalVerts;
    patchMesh.triangleCount = totalVerts / 3;
    patchMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    patchMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));

    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;
    int cursor = 0;

    for (int iz = 0; iz < segZ; iz++) {
        float z0 = -halfD + depth * ((float)iz / (float)segZ);
        float z1 = -halfD + depth * ((float)(iz + 1) / (float)segZ);
        float v0 = (float)iz / (float)segZ;
        float v1 = (float)(iz + 1) / (float)segZ;

        for (int ix = 0; ix < segX; ix++) {
            float x0 = -halfW + width * ((float)ix / (float)segX);
            float x1 = -halfW + width * ((float)(ix + 1) / (float)segX);
            float u0 = (float)ix / (float)segX;
            float u1 = (float)(ix + 1) / (float)segX;

            Vector3 p00 = {x0, 0.0f, z0};
            Vector3 p10 = {x1, 0.0f, z0};
            Vector3 p11 = {x1, 0.0f, z1};
            Vector3 p01 = {x0, 0.0f, z1};

            Vector2 uv00 = {u0, v0};
            Vector2 uv10 = {u1, v0};
            Vector2 uv11 = {u1, v1};
            Vector2 uv01 = {u0, v1};

            Water_SetVertex(&patchMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p10, uv10, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&patchMesh, cursor++, p01, uv01, WHITE);
        }
    }

    water.waterModel = Nature_ModelFromMesh(patchMesh, Water_GetShader());
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    // ── Build 3D Concave Basin Bed for Patch ───────────────────────────────
    Mesh patchBedMesh = {0};
    patchBedMesh.vertexCount = totalVerts;
    patchBedMesh.triangleCount = totalVerts / 3;
    patchBedMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchBedMesh.normals = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    patchBedMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    patchBedMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));
    int patchBedCursor = 0;
    for (int iz = 0; iz < segZ; iz++) {
        float z0 = -halfD + depth * ((float)iz / (float)segZ);
        float z1 = -halfD + depth * ((float)(iz + 1) / (float)segZ);
        float v0 = (float)iz / (float)segZ;
        float v1 = (float)(iz + 1) / (float)segZ;

        for (int ix = 0; ix < segX; ix++) {
            float x0 = -halfW + width * ((float)ix / (float)segX);
            float x1 = -halfW + width * ((float)(ix + 1) / (float)segX);
            float u0 = (float)ix / (float)segX;
            float u1 = (float)(ix + 1) / (float)segX;

            float y00 = -config.maxDepth * sinf(PI * u0) * sinf(PI * v0) - 0.015f;
            float y10 = -config.maxDepth * sinf(PI * u1) * sinf(PI * v0) - 0.015f;
            float y11 = -config.maxDepth * sinf(PI * u1) * sinf(PI * v1) - 0.015f;
            float y01 = -config.maxDepth * sinf(PI * u0) * sinf(PI * v1) - 0.015f;

            Vector3 p00 = {x0, y00, z0};
            Vector3 p10 = {x1, y10, z0};
            Vector3 p11 = {x1, y11, z1};
            Vector3 p01 = {x0, y01, z1};
            Vector3 n = {0.0f, 1.0f, 0.0f};

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};

            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p10, n, uv10, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&patchBedMesh, patchBedCursor++, p01, n, uv01, WHITE);
        }
    }
    water.bedModel = Nature_ModelFromMesh(patchBedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/stone_path_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    water.ready = true;
    return water;
}

MapWaterSurface MapProp_CreateWaterStrip(Vector3 start, Vector3 end, float width, MapWaterConfig config)
{
    MapWaterSurface water = {0};
    Vector3 diff = Vector3Subtract(end, start);
    float length = Vector3Length(diff);
    if (length < 0.1f || width <= 0.0f) return water;

    Vector3 forward = Vector3Scale(diff, 1.0f / length);
    Vector3 right = (Vector3){-forward.z, 0.0f, forward.x};

    Water_ApplyConfigDefaults(&config);
    config.center = start;
    config.radiusX = length * 0.5f;
    config.radiusZ = width * 0.5f;
    config.shape = WATER_SHAPE_STRIP;

    // If flow direction is unspecified, flow naturally along the stream channel
    if (config.flowVelocity.x == 0.0f && config.flowVelocity.y == 0.0f) {
        config.flowVelocity = (Vector2){forward.x * 0.85f, forward.z * 0.85f};
    }
    water.config = config;

    int segL = config.segments >= 4 ? config.segments : (int)(length * 2.0f);
    if (segL < 4) segL = 4;
    if (segL > 128) segL = 128;
    int segW = config.rings >= 2 ? config.rings : 4;
    int totalVerts = segL * segW * 6;

    Mesh stripMesh = {0};
    stripMesh.vertexCount = totalVerts;
    stripMesh.triangleCount = totalVerts / 3;
    stripMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    stripMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));

    float halfWidth = width * 0.5f;
    int cursor = 0;

    for (int il = 0; il < segL; il++) {
        float f0 = (float)il / (float)segL;
        float f1 = (float)(il + 1) / (float)segL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++) {
            float w0 = -halfWidth + width * ((float)iw / (float)segW);
            float w1 = -halfWidth + width * ((float)(iw + 1) / (float)segW);

            Vector3 p00 = Vector3Add(c0, Vector3Scale(right, w0));
            Vector3 p01 = Vector3Add(c0, Vector3Scale(right, w1));
            Vector3 p11 = Vector3Add(c1, Vector3Scale(right, w1));
            Vector3 p10 = Vector3Add(c1, Vector3Scale(right, w0));

            Vector2 uv00 = {f0, (float)iw / (float)segW};
            Vector2 uv01 = {f0, (float)(iw + 1) / (float)segW};
            Vector2 uv11 = {f1, (float)(iw + 1) / (float)segW};
            Vector2 uv10 = {f1, (float)iw / (float)segW};

            Water_SetVertex(&stripMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p01, uv01, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p00, uv00, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p11, uv11, WHITE);
            Water_SetVertex(&stripMesh, cursor++, p10, uv10, WHITE);
        }
    }

    water.waterModel = Nature_ModelFromMesh(stripMesh, Water_GetShader());
    Texture2D waterDetail = ResourceManager_LoadTexture("assets/textures/noise.png");
    SetTextureWrap(waterDetail, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(waterDetail, TEXTURE_FILTER_BILINEAR);
    water.waterModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = waterDetail;

    // ── Build 3D U-Channel Bed for Stream Strip ─────────────────────────────
    Mesh stripBedMesh = {0};
    stripBedMesh.vertexCount = totalVerts;
    stripBedMesh.triangleCount = totalVerts / 3;
    stripBedMesh.vertices = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripBedMesh.normals = MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    stripBedMesh.texcoords = MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    stripBedMesh.colors = MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));
    int stripBedCursor = 0;
    for (int il = 0; il < segL; il++) {
        float f0 = (float)il / (float)segL;
        float f1 = (float)(il + 1) / (float)segL;
        Vector3 c0 = Vector3Scale(forward, f0 * length);
        Vector3 c1 = Vector3Scale(forward, f1 * length);

        for (int iw = 0; iw < segW; iw++) {
            float w0 = -halfWidth + width * ((float)iw / (float)segW);
            float w1 = -halfWidth + width * ((float)(iw + 1) / (float)segW);
            float nw0 = w0 / halfWidth;
            float nw1 = w1 / halfWidth;
            float y0 = -config.maxDepth * (1.0f - nw0 * nw0) - 0.015f;
            float y1 = -config.maxDepth * (1.0f - nw1 * nw1) - 0.015f;

            Vector3 p00 = Vector3Add(c0, Vector3Scale(right, w0)); p00.y = y0;
            Vector3 p01 = Vector3Add(c0, Vector3Scale(right, w1)); p01.y = y1;
            Vector3 p11 = Vector3Add(c1, Vector3Scale(right, w1)); p11.y = y1;
            Vector3 p10 = Vector3Add(c1, Vector3Scale(right, w0)); p10.y = y0;
            Vector3 n = {0.0f, 1.0f, 0.0f};

            Vector2 uv00 = {p00.x * 0.16f, p00.z * 0.16f};
            Vector2 uv01 = {p01.x * 0.16f, p01.z * 0.16f};
            Vector2 uv11 = {p11.x * 0.16f, p11.z * 0.16f};
            Vector2 uv10 = {p10.x * 0.16f, p10.z * 0.16f};

            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p01, n, uv01, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p00, n, uv00, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p11, n, uv11, WHITE);
            Water_SetBedVertex(&stripBedMesh, stripBedCursor++, p10, n, uv10, WHITE);
        }
    }
    water.bedModel = Nature_ModelFromMesh(stripBedMesh, Water_GetBedShader());
    water.bedDiffuseTex = ResourceManager_LoadTexture("assets/textures/stone_path_diffuse.png");
    SetTextureWrap(water.bedDiffuseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.bedDiffuseTex, TEXTURE_FILTER_BILINEAR);
    water.bedModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = water.bedDiffuseTex;

    water.causticTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    SetTextureWrap(water.causticTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(water.causticTex, TEXTURE_FILTER_BILINEAR);

    water.ready = true;
    return water;
}

Vector3 MapProp_GetWaterEdgePoint(const MapWaterSurface *water, float angleRad,
                                  float radialScale)
{
    if (!water)
        return (Vector3){0};
    float edge = Water_EdgeScale(angleRad, 1.0f, water->config.seed);
    return (Vector3){
        water->config.center.x + cosf(angleRad) * water->config.radiusX * radialScale * edge,
        water->config.center.y,
        water->config.center.z + sinf(angleRad) * water->config.radiusZ * radialScale * edge,
    };
}

bool MapProp_SampleWaterBed(const MapWaterSurface *water, float x, float z,
                            float *outBedHeight, Vector3 *outNormal)
{
    if (!water || !water->ready) return false;

    if (water->config.shape == WATER_SHAPE_RADIAL) {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        float angle = atan2f(dz, dx);
        float edge = Water_EdgeScale(angle, 1.0f, water->config.seed);
        float rx = water->config.radiusX * edge;
        float rz = water->config.radiusZ * edge;
        if (rx <= 0.001f || rz <= 0.001f) return false;

        float nx = dx / rx;
        float nz = dz / rz;
        float r2 = nx * nx + nz * nz;
        if (r2 >= 1.0f) return false; // Nằm ngoài chu vi mặt hồ

        float r = sqrtf(r2);
        float depth = water->config.maxDepth * (1.0f - powf(r, 1.6f));
        if (outBedHeight) {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal) {
            float slope = 1.6f * powf(fmaxf(0.01f, r), 0.6f) * (water->config.maxDepth / fmaxf(rx, rz));
            Vector3 n = {nx * slope, 1.0f, nz * slope};
            *outNormal = Vector3Normalize(n);
        }
        return true;
    } else if (water->config.shape == WATER_SHAPE_RECT) {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        float halfW = water->config.radiusX;
        float halfD = water->config.radiusZ;
        if (fabsf(dx) >= halfW || fabsf(dz) >= halfD) return false;

        float u = (dx + halfW) / (2.0f * halfW);
        float v = (dz + halfD) / (2.0f * halfD);
        float depth = water->config.maxDepth * sinf(PI * u) * sinf(PI * v);
        if (outBedHeight) {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal) {
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        }
        return true;
    } else if (water->config.shape == WATER_SHAPE_STRIP) {
        float dx = x - water->config.center.x;
        float dz = z - water->config.center.z;
        Vector2 flowDir = {water->config.flowVelocity.x, water->config.flowVelocity.y};
        float flowLen = Vector2Length(flowDir);
        Vector3 forward = (flowLen > 0.001f) ? (Vector3){flowDir.x / flowLen, 0.0f, flowDir.y / flowLen} : (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 right = {-forward.z, 0.0f, forward.x};

        float distLong = dx * forward.x + dz * forward.z;
        float distCross = dx * right.x + dz * right.z;
        float halfLen = water->config.radiusX;
        float halfWid = water->config.radiusZ;
        if (distLong < 0.0f || distLong > 2.0f * halfLen || fabsf(distCross) >= halfWid) return false;

        float w = distCross / halfWid;
        float depth = water->config.maxDepth * (1.0f - w * w);
        if (outBedHeight) {
            *outBedHeight = water->config.center.y - depth - 0.015f;
        }
        if (outNormal) {
            *outNormal = (Vector3){0.0f, 1.0f, 0.0f};
        }
        return true;
    }

    return false;
}

void MapProp_DrawWaterBed(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready) return;
    // Upload while a Vulkan frame is recording. Uploading from the map update
    // path waits for every in-flight frame and can stall once per frame.
    if (water->waveFieldTex.id > 0 && water->wavePixels)
        UpdateTexture(water->waveFieldTex, water->wavePixels);
    Vector3 position = water->config.center;

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector3 sunRgb = {sun.x, sun.y, sun.z};
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};

    // 1. Draw 3D Concave Lake Bed (Nền hồ 3D thật có texture sỏi đá và tụ quang sóng)
    if (water->bedModel.meshCount > 0) {
        Shader bedShader = Water_GetBedShader();
        SetShaderValue(bedShader, s_bedLocTime, &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocWaterHeight, &water->config.center.y, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocLightDir, &lightDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocLightColor, &sunRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocAmbientColor, &ambientRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocCausticsStrength, &water->config.causticsStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocCausticsScale, &water->config.causticsScale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(bedShader, s_bedLocAbsorption, &water->config.absorption, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocDeepColor, &deepRgb, SHADER_UNIFORM_VEC3);
        SetShaderValue(bedShader, s_bedLocShallowColor, &shallowRgb, SHADER_UNIFORM_VEC3);
        if (s_bedLocModelPos >= 0) SetShaderValue(bedShader, s_bedLocModelPos, &position, SHADER_UNIFORM_VEC3);

        Texture2D cTex = (water->causticTex.id > 0) ? water->causticTex : s_defaultCausticTex;
        rlActiveTextureSlot(1);
        rlEnableTexture(cTex.id);

        rlActiveTextureSlot(0);
        DrawModel(water->bedModel, position, 1.0f, WHITE);

        rlActiveTextureSlot(1);
        rlDisableTexture();
        rlActiveTextureSlot(0);
    }

    // 2. Draw Shoreline Bank Rim (Dải bờ đất ven hồ)
    if (water->bankModel.meshCount > 0) {
        Shader bankShader = Nature_GetShader(false);
        Nature_BeginWindReceiverShader(bankShader);
        Nature_UpdateShader(bankShader, time, (Vector2){0.0f, 0.0f}, 0.0f,
                            false, 1.0f, NATURE_WIND_RESPONSE_STATIC);
        int noInteraction = 0;
        SetShaderValue(bankShader, GetShaderLocation(bankShader, "u_interactionEnabled"),
                       &noInteraction, SHADER_UNIFORM_INT);
        SetShaderValue(bankShader, GetShaderLocation(bankShader, "u_windImpactEnabled"),
                       &noInteraction, SHADER_UNIFORM_INT);
        rlDisableBackfaceCulling();
        DrawModel(water->bankModel, position, 1.0f, WHITE);
        Nature_EndWindReceiverShader();
    }
}

void MapProp_DrawWaterOverlay(const MapWaterSurface *water, float time)
{
    if (!water || !water->ready) return;
    Vector3 position = water->config.center;

    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector4 deep = ColorNormalize(water->config.deepColor);
    Vector4 shallow = ColorNormalize(water->config.shallowColor);
    Vector4 foam = ColorNormalize(water->config.foamColor);
    Vector3 sunRgb = {sun.x, sun.y, sun.z};
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Vector3 deepRgb = {deep.x, deep.y, deep.z};
    Vector3 shallowRgb = {shallow.x, shallow.y, shallow.z};
    Vector3 foamRgb = {foam.x, foam.y, foam.z};

    // Draw Crystal Clear Water Surface with Alpha Blending
    Shader shader = Water_GetShader();
    SetShaderValue(shader, s_waterLocTime, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveHeight, &water->config.waveHeight, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveScale, &water->config.waveScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocWaveSpeed, &water->config.waveSpeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocDetailScale, &water->config.detailScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocDetailStrength, &water->config.detailStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocFlowVelocity, &water->config.flowVelocity, SHADER_UNIFORM_VEC2);
    int shapeInt = (int)water->config.shape;
    SetShaderValue(shader, s_waterLocWaterShape, &shapeInt, SHADER_UNIFORM_INT);

    SetShaderValue(shader, s_waterLocLightDir, &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocLightColor, &sunRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocAmbientColor, &ambientRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocViewPos, &camera.position, SHADER_UNIFORM_VEC3);
    if (s_waterLocModelPos >= 0) SetShaderValue(shader, s_waterLocModelPos, &position, SHADER_UNIFORM_VEC3);

    SetShaderValue(shader, s_waterLocMaxDepth, &water->config.maxDepth, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocAbsorption, &water->config.absorption, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocDeepColor, &deepRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocShallowColor, &shallowRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocFoamColor, &foamRgb, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocScatterColor, &water->config.scatterColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, s_waterLocScatterCoeff, &water->config.scatterCoeff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocCausticsStrength, &water->config.causticsStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocCausticsScale, &water->config.causticsScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, s_waterLocFoamThreshold, &water->config.foamThreshold, SHADER_UNIFORM_FLOAT);
    int waveFieldEnabled = water->waveFieldTex.id > 0 ? 1 : 0;
    if (s_waterLocWaveFieldEnabled >= 0)
        SetShaderValue(shader, s_waterLocWaveFieldEnabled, &waveFieldEnabled, SHADER_UNIFORM_INT);

    // Dynamic interactor, impacts, and persistent flow around submerged rocks.
    ((MapWaterSurface*)water)->lastTime = time;
    Vector3 interaction = {water->interactorPos.x, water->interactorSubmerged, water->interactorPos.z};
    if (s_waterLocInteractor >= 0)
        SetShaderValue(shader, s_waterLocInteractor, &interaction, SHADER_UNIFORM_VEC3);
    if (s_waterLocVelocity >= 0)
        SetShaderValue(shader, s_waterLocVelocity, &water->interactorVel, SHADER_UNIFORM_VEC3);
    if (s_waterLocRadius >= 0)
        SetShaderValue(shader, s_waterLocRadius, &water->interactorRadius, SHADER_UNIFORM_FLOAT);

    Vector4 rings[MAX_WATER_RIPPLES];
    Vector4 params[MAX_WATER_RIPPLES];
    for (int i = 0; i < MAX_WATER_RIPPLES; i++) {
        if (water->ripples[i].active) {
            rings[i] = (Vector4){
                water->ripples[i].position.x,
                water->ripples[i].position.z,
                water->ripples[i].spawnTime,
                water->ripples[i].maxRadius
            };
            params[i] = (Vector4){
                water->ripples[i].amplitude,
                water->ripples[i].speed,
                water->ripples[i].wavelength,
                water->ripples[i].decay
            };
        } else {
            rings[i] = (Vector4){0.0f, 0.0f, -100.0f, 0.0f};
            params[i] = (Vector4){0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
    for (int i = 0; i < MAX_WATER_RIPPLES; i++) {
        if (s_waterLocRippleRings[i] >= 0)
            SetShaderValue(shader, s_waterLocRippleRings[i], &rings[i], SHADER_UNIFORM_VEC4);
        if (s_waterLocRippleParams[i] >= 0)
            SetShaderValue(shader, s_waterLocRippleParams[i], &params[i], SHADER_UNIFORM_VEC4);
    }
    for (int i = 0; i < MAX_WATER_OBSTACLES; i++) {
        Vector4 rock = i < water->obstacleCount ? water->obstacles[i] : (Vector4){0};
        if (s_waterLocObstacles[i] >= 0)
            SetShaderValue(shader, s_waterLocObstacles[i], &rock, SHADER_UNIFORM_VEC4);
    }

    // Multi-Texture Bindings
    Texture2D cTex = (water->causticTex.id > 0) ? water->causticTex : s_defaultCausticTex;
    rlActiveTextureSlot(1);
    rlEnableTexture(cTex.id);
    if (waveFieldEnabled) {
        rlActiveTextureSlot(3);
        rlEnableTexture(water->waveFieldTex.id);
    }

    SceneTargets_RequestSoftDepthRegion((Rectangle){ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() });
    Vector2 screenRes = { (float)GetScreenWidth(), (float)GetScreenHeight() };
    if (s_waterLocResolution >= 0) SetShaderValue(shader, s_waterLocResolution, &screenRes, SHADER_UNIFORM_VEC2);

    Texture2D depthTex = SceneTargets_GetDepthTexture();
    int hasDepth = (depthTex.id > 0) ? 1 : 0;
    SetShaderValue(shader, s_waterLocHasDepthTex, &hasDepth, SHADER_UNIFORM_INT);
    if (hasDepth) {
        rlActiveTextureSlot(2);
        rlEnableTexture(depthTex.id);
    }

    rlActiveTextureSlot(0);
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    DrawModel(water->waterModel, position, 1.0f, WHITE);

    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();

    // Restore Texture Slots
    if (hasDepth) {
        rlActiveTextureSlot(2);
        rlDisableTexture();
    }
    if (waveFieldEnabled) {
        rlActiveTextureSlot(3);
        rlDisableTexture();
    }
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
}

void MapProp_DrawWaterSurface(const MapWaterSurface *water, float time)
{
    MapProp_DrawWaterBed(water, time);
    MapProp_DrawWaterOverlay(water, time);
}

void MapProp_SetWaterInteractor(MapWaterSurface *water, Vector3 position, Vector3 velocity,
                                float radius, float submerged)
{
    if (!water || !water->ready) return;
    water->interactorPos = position;
    water->interactorVel = velocity;
    water->interactorRadius = radius;
    water->interactorSubmerged = submerged;
}

void MapProp_AddWaterRipple(MapWaterSurface *water, Vector3 position, float radius, float intensity)
{
    if (!water || !water->ready) return;
    bool isStepEvent = intensity > 0.0f && intensity <= 0.65f;
    if (isStepEvent && water->lastTime - water->waveLastImpulseTime < 0.12f) return;
    if (isStepEvent) water->waveLastImpulseTime = water->lastTime;
    int idx = water->nextRipple;
    // Wading impulses come from alternating feet, not the torso center.
    // Keep jumps and hard impacts centered.
    float planarSpeed = hypotf(water->interactorVel.x, water->interactorVel.z);
    bool footstep = intensity > 0.0f && intensity <= 0.65f && planarSpeed > 0.2f;
    if (footstep) {
        float side = (idx & 1) ? 1.0f : -1.0f;
        float footOffset = fmaxf(0.12f, water->interactorRadius * 0.48f);
        position.x += -water->interactorVel.z / planarSpeed * footOffset * side;
        position.z +=  water->interactorVel.x / planarSpeed * footOffset * side;
    }
    water->ripples[idx].position = position;
    water->ripples[idx].spawnTime = water->lastTime;
    water->ripples[idx].maxRadius = footstep ? fminf(radius, 1.8f) : ((radius > 0.0f) ? radius : 6.0f);
    water->ripples[idx].amplitude = (intensity > 0.0f) ? intensity : 1.0f;
    water->ripples[idx].speed = footstep ? (2.25f + 0.16f * (float)(idx & 1)) : 3.2f;
    water->ripples[idx].wavelength = footstep ? (0.42f + 0.05f * (float)(idx & 1)) : 0.52f;
    water->ripples[idx].decay = footstep ? 2.2f : 1.8f;
    water->ripples[idx].active = true;
    water->nextRipple = (water->nextRipple + 1) % MAX_WATER_RIPPLES;

    if (water->waveVelocityField) {
        const int n = WATER_FIELD_SIZE;
        float dx = 2.0f * water->config.radiusX / (float)(n - 1);
        float dz = 2.0f * water->config.radiusZ / (float)(n - 1);
        float sigma = footstep ? 0.22f : 0.30f;
        float impulse = footstep ? fminf(1.2f, intensity * 2.35f)
                                 : fminf(1.1f, fmaxf(0.0f, intensity) * 0.9f);
        int cx = (int)roundf((position.x - water->config.center.x + water->config.radiusX) / dx);
        int cz = (int)roundf((position.z - water->config.center.z + water->config.radiusZ) / dz);
        int reachX = (int)ceilf(3.0f * sigma / dx);
        int reachZ = (int)ceilf(3.0f * sigma / dz);
        for (int z = fmaxf(1, cz - reachZ); z <= fminf(n - 2, cz + reachZ); z++) {
            float wz = water->config.center.z - water->config.radiusZ + z * dz;
            for (int x = fmaxf(1, cx - reachX); x <= fminf(n - 2, cx + reachX); x++) {
                float wx = water->config.center.x - water->config.radiusX + x * dx;
                float nx = (wx - water->config.center.x) / water->config.radiusX;
                float nz = (wz - water->config.center.z) / water->config.radiusZ;
                if (nx * nx + nz * nz >= 0.94f) continue;
                float dist2 = (wx - position.x) * (wx - position.x) +
                              (wz - position.z) * (wz - position.z);
                float q = dist2 / (2.0f * sigma * sigma);
                water->waveVelocityField[z * n + x] += impulse * expf(-q) * (1.0f - q);
            }
        }
    }
}

void MapProp_AddWaterObstacle(MapWaterSurface *water, Vector3 position, float radius, float strength)
{
    if (!water || !water->ready || water->obstacleCount >= MAX_WATER_OBSTACLES || radius <= 0.0f) return;
    water->obstacles[water->obstacleCount++] = (Vector4){position.x, position.z, radius, strength};
}

void MapProp_UpdateWaterSurface(MapWaterSurface *water, float dt)
{
    if (!water || !water->waveFieldTex.id || !water->waveHeightField || dt <= 0.0f) return;
    float motionSpeed = hypotf(water->interactorVel.x, water->interactorVel.z);
    if (water->interactorSubmerged > 0.05f && motionSpeed > 0.35f) {
        water->waveWakeTimer += dt;
        if (water->waveWakeTimer >= 0.24f) {
            water->waveWakeTimer -= 0.24f;
            MapProp_AddWaterRipple(water, water->interactorPos, 1.8f, 0.48f);
        }
    } else {
        water->waveWakeTimer = 0.12f;
    }
    const int n = WATER_FIELD_SIZE;
    const float step = 1.0f / 60.0f;
    const float dx = 2.0f * water->config.radiusX / (float)(n - 1);
    const float dz = 2.0f * water->config.radiusZ / (float)(n - 1);
    const float invDx2 = 1.0f / (dx * dx);
    const float invDz2 = 1.0f / (dz * dz);
    water->waveStepRemainder = fminf(water->waveStepRemainder + dt, 3.0f * step);
    while (water->waveStepRemainder >= step) {
        water->waveStepRemainder -= step;
        for (int z = 1; z < n - 1; z++) {
            float nz = 2.0f * (float)z / (float)(n - 1) - 1.0f;
            for (int x = 1; x < n - 1; x++) {
                int i = z * n + x;
                float nx = 2.0f * (float)x / (float)(n - 1) - 1.0f;
                float radial2 = nx * nx + nz * nz;
                bool blocked = radial2 >= 0.94f;
                if (!blocked) {
                    float wx = water->config.center.x + nx * water->config.radiusX;
                    float wz = water->config.center.z + nz * water->config.radiusZ;
                    for (int k = 0; k < water->obstacleCount; k++) {
                        float ox = wx - water->obstacles[k].x;
                        float oz = wz - water->obstacles[k].y;
                        if (ox * ox + oz * oz < water->obstacles[k].z * water->obstacles[k].z) {
                            blocked = true;
                            break;
                        }
                    }
                }
                if (blocked) {
                    water->waveNextField[i] = 0.0f;
                    water->waveVelocityField[i] = 0.0f;
                    continue;
                }
                float h = water->waveHeightField[i];
                float lap = (water->waveHeightField[i - 1] + water->waveHeightField[i + 1] - 2.0f * h) * invDx2 +
                            (water->waveHeightField[i - n] + water->waveHeightField[i + n] - 2.0f * h) * invDz2;
                float speedSquared = (2.35f * 2.35f) * fmaxf(0.08f, 1.0f - radial2);
                float shoreDamping = 1.4f + 4.0f * fmaxf(0.0f, radial2 - 0.70f);
                float velocity = (water->waveVelocityField[i] + speedSquared * lap * step) *
                                 fmaxf(0.0f, 1.0f - shoreDamping * step);
                water->waveVelocityField[i] = velocity;
                water->waveNextField[i] = fmaxf(-0.12f, fminf(0.12f, h + velocity * step));
            }
        }
        float *old = water->waveHeightField;
        water->waveHeightField = water->waveNextField;
        water->waveNextField = old;
    }
    for (int z = 0; z < n; z++) {
        for (int x = 0; x < n; x++) {
            int i = z * n + x;
            int xl = x > 0 ? i - 1 : i;
            int xr = x < n - 1 ? i + 1 : i;
            int zb = z > 0 ? i - n : i;
            int zf = z < n - 1 ? i + n : i;
            float sx = (water->waveHeightField[xr] - water->waveHeightField[xl]) / (2.0f * dx);
            float sz = (water->waveHeightField[zf] - water->waveHeightField[zb]) / (2.0f * dz);
            float h = water->waveHeightField[i];
            water->wavePixels[i * 4 + 0] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + sx)));
            water->wavePixels[i * 4 + 1] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + sz)));
            water->wavePixels[i * 4 + 2] = (unsigned char)(255.0f * fminf(1.0f, fmaxf(0.0f, 0.5f + h * 4.0f)));
            float lap = (water->waveHeightField[xl] + water->waveHeightField[xr] - 2.0f * h) * invDx2 +
                        (water->waveHeightField[zb] + water->waveHeightField[zf] - 2.0f * h) * invDz2;
            water->wavePixels[i * 4 + 3] = (unsigned char)(255.0f *
                fminf(1.0f, fmaxf(0.0f, 0.5f - lap * 0.035f)));
        }
    }
}

void MapProp_UnloadWaterSurface(MapWaterSurface *water)
{
    if (!water || !water->ready) return;
    UnloadModel(water->waterModel);
    if (water->bankModel.meshCount > 0) {
        UnloadModel(water->bankModel);
    }
    if (water->bedModel.meshCount > 0) {
        UnloadModel(water->bedModel);
    }
    if (water->waveFieldTex.id > 0) UnloadTexture(water->waveFieldTex);
    free(water->waveHeightField);
    free(water->waveNextField);
    free(water->waveVelocityField);
    free(water->wavePixels);
    water->ready = false;
}
