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
static int s_waterLocLakeRadii = -1;
static int s_waterLocLakeSeed = -1;

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
// Local airflow uses stronger canopy compliance than broad ambient wind.
// Both analytic impacts and spatial fields retain the existing 0.55 m cap.
static const float kNatureWindBendPerMps = 0.10f;
// Shared interaction field represents grass canopy, not the airborne source.
static const float kNatureWindSampleHeight = 0.25f;
static Texture2D s_natureInteractionTexture = {0};
static Color s_natureInteractionPixels[NATURE_INTERACTION_PIXEL_COUNT];
static Color s_natureInteractionScratch[NATURE_INTERACTION_PIXEL_COUNT];
static Color s_natureInteractionUploaded[NATURE_INTERACTION_PIXEL_COUNT];
static bool s_natureInteractionUploadValid = false;
static Color s_natureWindPixels[NATURE_INTERACTION_PIXEL_COUNT];
// Tip displacement of a rooted canopy element; independent of pixel recovery.
static Vector2 s_natureForceBend[NATURE_INTERACTION_PIXEL_COUNT];
static Vector2 s_natureForceVelocity[NATURE_INTERACTION_PIXEL_COUNT];
static Vector2 s_natureForceBendScratch[NATURE_INTERACTION_PIXEL_COUNT];
static Vector2 s_natureForceVelocityScratch[NATURE_INTERACTION_PIXEL_COUNT];
static const float kNatureCanopyMassKg = 0.003f;
static float s_natureForceDt;
static bool s_natureForceAwake;

static Vector2 s_natureInteractionCenter = {0};
static bool s_natureInteractionReady = false;
static bool s_natureInteractionOpen = false;
static bool s_natureWindReceiverReady = false;
static WindMacroConfig s_natureWindMacro = {0};
static bool s_natureWindImpactEnabled = false;
static Vector2 s_natureWindImpactCenter = {0};
static Vector2 s_natureWindImpactDirection = {1.0f, 0.0f};
static int s_natureWindImpactType = 0;
static float s_natureWindImpactRadius = 0.0f;
static float s_natureWindImpactStrength = 0.0f;
static float s_natureWindImpactAge = 0.0f;
static int s_natureWindTraceDominantSlot = -1;
static float s_natureWindTraceNextTime = 0.0f;
static bool s_natureWindTraceVisibleUploaded = false;
static bool s_natureWindTraceShadowUploaded = false;
static MapNatureRenderStats s_natureRenderStats = {0};

// Cache the Core macro field once per frame instead of evaluating sixteen
// gradient-noise corners for every duplicated visible/shadow mesh vertex.
#define NATURE_MACRO_RESOLUTION 16
static const float kNatureMacroWorldSize = 64.0f;
static const float kNatureMacroInterval = 1.0f / 15.0f;
static Vector2 s_natureMacroTimeRange = {0};
static WindMacroConfig s_natureMacroCachedConfig = {0};
static Texture2D s_natureMacroTexture = {0};
static Color s_natureMacroPixels[NATURE_MACRO_RESOLUTION * NATURE_MACRO_RESOLUTION];
static Vector4 s_natureMacroRegion = {0}; // centre x/z, size, reference height
static float s_natureMacroScale = 1.0f;
static bool s_natureMacroReady = false;
static bool s_natureMacroVerified = false;

static unsigned char Nature_EncodeMacroComponent(float value)
{
    float normalized = value / s_natureMacroScale;
    return (unsigned char)roundf((fmaxf(-1.0f, fminf(1.0f, normalized)) * 0.5f + 0.5f) * 255.0f);
}

static Vector2 Nature_DecodeMacroAtCell(int x, int z, float fraction)
{
    Color pixel = s_natureMacroPixels[z * NATURE_MACRO_RESOLUTION + x];
    return (Vector2){
        (((float)pixel.r + ((float)pixel.b - pixel.r) * fraction) / 255.0f * 2.0f - 1.0f) * s_natureMacroScale,
        (((float)pixel.g + ((float)pixel.a - pixel.g) * fraction) / 255.0f * 2.0f - 1.0f) * s_natureMacroScale,
    };
}

static bool Nature_WindTraceEnabled(void);

static void Nature_VerifyMacroCache(void)
{
    const char *trace = getenv("WUXING_WIND_RECEIVER_TRACE");
    if (s_natureMacroVerified || !trace || trace[0] == '\0' || trace[0] == '0')
        return;
    s_natureMacroVerified = true;
    float cellSize = kNatureMacroWorldSize / NATURE_MACRO_RESOLUTION;
    float sumErrorSq = 0.0f;
    float maxError = 0.0f;
    for (int z = 0; z < 4; z++) {
        for (int x = 0; x < 4; x++) {
            // Sample off-grid, halfway through the cached interval, at blade
            // height rather than the horizontal reference plane.
            float gridX = 1.37f + (float)x * 8.0f;
            float gridZ = 1.61f + (float)z * 8.0f;
            int ix = (int)gridX;
            int iz = (int)gridZ;
            Vector2 lower = Vector2Lerp(Nature_DecodeMacroAtCell(ix, iz, 0.5f),
                                       Nature_DecodeMacroAtCell(ix + 1, iz, 0.5f), gridX - ix);
            Vector2 upper = Vector2Lerp(Nature_DecodeMacroAtCell(ix, iz + 1, 0.5f),
                                       Nature_DecodeMacroAtCell(ix + 1, iz + 1, 0.5f), gridX - ix);
            Vector2 cached = Vector2Lerp(lower, upper, gridZ - iz);
            Vector3 position = {
                s_natureMacroRegion.x - kNatureMacroWorldSize * 0.5f + (gridX + 0.5f) * cellSize,
                s_natureMacroRegion.w + 0.35f,
                s_natureMacroRegion.y - kNatureMacroWorldSize * 0.5f + (gridZ + 0.5f) * cellSize,
            };
            Vector3 exact = Wind_GetMacroAt(position, s_natureMacroTimeRange.x + kNatureMacroInterval * 0.5f);
            float dx = cached.x - exact.x;
            float dz = cached.y - exact.z;
            float errorSq = dx * dx + dz * dz;
            sumErrorSq += errorSq;
            maxError = fmaxf(maxError, sqrtf(errorSq));
        }
    }
    TraceLog(LOG_INFO, "NATURE_MACRO_CACHE_PARITY: samples=16 rms_error_mps=%.4f max_error_mps=%.4f",
             sqrtf(sumErrorSq / 16.0f), maxError);
}

static void Nature_UpdateMacroCache(float time)
{
    // High-frequency fields use the analytic fallback; a coarse horizontal
    // cache must not erase their structure. Core macro wind itself does not
    // apply the height-gradient modifier (that belongs to aggregate wind).
    if (s_natureWindMacro.gustAmplitude <= 0.0001f ||
        Vector3LengthSqr(s_natureWindMacro.baseDirection) < 0.00000001f ||
        s_natureWindMacro.noiseScale > 0.15f) {
        s_natureMacroReady = false;
        return;
    }
    float cellSize = kNatureMacroWorldSize / NATURE_MACRO_RESOLUTION;
    Vector4 region = {
        roundf(camera.target.x / cellSize) * cellSize,
        roundf(camera.target.z / cellSize) * cellSize,
        kNatureMacroWorldSize, camera.target.y,
    };
    float sampleTime = fmaxf(0.0f, time - 0.035f);
    bool sameMacro = Vector3Equals(s_natureWindMacro.baseDirection, s_natureMacroCachedConfig.baseDirection) &&
        s_natureWindMacro.gustAmplitude == s_natureMacroCachedConfig.gustAmplitude &&
        s_natureWindMacro.noiseScale == s_natureMacroCachedConfig.noiseScale &&
        s_natureWindMacro.noiseSpeed == s_natureMacroCachedConfig.noiseSpeed;
    if (s_natureMacroReady && sameMacro &&
        region.x == s_natureMacroRegion.x && region.y == s_natureMacroRegion.y &&
        fabsf(region.w - s_natureMacroRegion.w) < 0.25f &&
        sampleTime >= s_natureMacroTimeRange.x &&
        sampleTime <= s_natureMacroTimeRange.x + kNatureMacroInterval)
        return;
    s_natureMacroRegion = region;
    s_natureMacroTimeRange = (Vector2){sampleTime, kNatureMacroInterval};
    s_natureMacroCachedConfig = s_natureWindMacro;
    float baseSpeed = Vector3Length(s_natureWindMacro.baseDirection);
    float gust = fmaxf(s_natureWindMacro.gustAmplitude, 0.0f);
    float maxBase = fmaxf(fabsf(s_natureWindMacro.baseDirection.x),
                         fabsf(s_natureWindMacro.baseDirection.z));
    s_natureMacroScale = fmaxf(1.0f, maxBase * (1.0f + gust)
                                  + gust * fmaxf(baseSpeed, 2.0f));
    for (int z = 0; z < NATURE_MACRO_RESOLUTION; z++) {
        for (int x = 0; x < NATURE_MACRO_RESOLUTION; x++) {
            Vector3 position = {
                s_natureMacroRegion.x + ((float)x + 0.5f) * cellSize - kNatureMacroWorldSize * 0.5f,
                s_natureMacroRegion.w,
                s_natureMacroRegion.y + ((float)z + 0.5f) * cellSize - kNatureMacroWorldSize * 0.5f,
            };
            Vector3 grass = Wind_GetMacroAt(position, sampleTime);
            Vector3 future = Wind_GetMacroAt(position, sampleTime + kNatureMacroInterval);
            s_natureMacroPixels[z * NATURE_MACRO_RESOLUTION + x] = (Color){
                Nature_EncodeMacroComponent(grass.x), Nature_EncodeMacroComponent(grass.z),
                Nature_EncodeMacroComponent(future.x), Nature_EncodeMacroComponent(future.z),
            };
        }
    }
    if (s_natureMacroTexture.id == 0) {
        Image image = {
            .data = s_natureMacroPixels, .width = NATURE_MACRO_RESOLUTION,
            .height = NATURE_MACRO_RESOLUTION, .mipmaps = 1,
            .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
        };
        s_natureMacroTexture = LoadTextureFromImage(image);
        if (s_natureMacroTexture.id != 0) {
            SetTextureFilter(s_natureMacroTexture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_natureMacroTexture, TEXTURE_WRAP_CLAMP);
        }
    } else {
        UpdateTexture(s_natureMacroTexture, s_natureMacroPixels);
    }
    s_natureMacroReady = s_natureMacroTexture.id != 0;
    if (s_natureMacroReady && Nature_WindTraceEnabled())
        Nature_VerifyMacroCache();

}


static bool Nature_WindTraceEnabled(void)
{
    const char *value = getenv("WUXING_WIND_RECEIVER_TRACE");
    return value != NULL && value[0] != '\0' && value[0] != '0';
}

// Under rlvk SetShaderValue targets the currently active shader, not merely
// the Shader argument. Keep every vegetation uniform upload and its draw in
// one explicit scope so custom wind uniforms cannot land on the previous pass:
/*
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShader
    Nature_BeginWindReceiverShader(shader);
    Nature_UpdateShadowShader
*/
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
    s_natureInteractionUploadValid = false;
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
            int destination = y * NATURE_INTERACTION_RESOLUTION + x;
            Vector2 bend = {0}, velocity = {0};
            if (sourceX >= 0 && sourceX < NATURE_INTERACTION_RESOLUTION &&
                sourceY >= 0 && sourceY < NATURE_INTERACTION_RESOLUTION) {
                int source = sourceY * NATURE_INTERACTION_RESOLUTION + sourceX;
                bend = s_natureForceBend[source];
                velocity = s_natureForceVelocity[source];
            }
            s_natureForceBendScratch[destination] = bend;
            s_natureForceVelocityScratch[destination] = velocity;
            s_natureInteractionScratch[y * NATURE_INTERACTION_RESOLUTION + x] = value;
        }
    }
    for (int i = 0; i < NATURE_INTERACTION_PIXEL_COUNT; i++)
        s_natureInteractionPixels[i] = s_natureInteractionScratch[i];
    memcpy(s_natureForceBend, s_natureForceBendScratch, sizeof(s_natureForceBend));
    memcpy(s_natureForceVelocity, s_natureForceVelocityScratch, sizeof(s_natureForceVelocity));
    s_natureInteractionCenter = newCenter;
}

void MapProp_BeginNatureInteraction(Vector3 focus, float dt)
{
    Nature_InitInteraction();
    s_natureForceDt = fminf(fmaxf(dt, 0.0f), 0.1f);
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
        (s_natureWindTraceDominantSlot != directImpact ||
         time >= s_natureWindTraceNextTime)) {
        s_natureWindTraceNextTime = time + 0.5f;
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

// Rooted vegetation responds to force through material mass and flexure, never
// free-body capture/arrival. Underdamped release gives an opposite return beat.
static void Nature_AdvanceAnchoredCell(Vector2 *bend, Vector2 *velocity,
                                      Vector3 forceNewtons, float dt)
{
    const float massKg = kNatureCanopyMassKg;
    const float stiffnessNPerM = 0.30f;
    const float dampingRatio = 0.30f;
    const float damping = 2.0f * dampingRatio * sqrtf(stiffnessNPerM * massKg);
    float step = fminf(fmaxf(dt, 0.0f), 1.0f / 120.0f);
    float implicitDrag = 1.0f + damping * step / massKg;
    velocity->x = (velocity->x + (forceNewtons.x - stiffnessNPerM * bend->x) * step / massKg) / implicitDrag;
    velocity->y = (velocity->y + (forceNewtons.z - stiffnessNPerM * bend->y) * step / massKg) / implicitDrag;
    bend->x += velocity->x * step;
    bend->y += velocity->y * step;
    float length = sqrtf(bend->x * bend->x + bend->y * bend->y);
    if (length > kNatureInteractionMaxBend) {
        Vector2 normal = {bend->x / length, bend->y / length};
        bend->x = normal.x * kNatureInteractionMaxBend;
        bend->y = normal.y * kNatureInteractionMaxBend;
        float outwardSpeed = velocity->x * normal.x + velocity->y * normal.y;
        if (outwardSpeed > 0.0f) {
            velocity->x -= normal.x * outwardSpeed;
            velocity->y -= normal.y * outwardSpeed;
        }
    }
}

static Vector3 Nature_AnchoredAirForce(Vector3 velocity, Vector3 airflow, float dt)
{
    // Only target-authored airflow is coupled here; ordinary Wind already has
    // its own receiver path. A representative blade projects height * width.
    float airSpeed2 = airflow.x * airflow.x + airflow.y * airflow.y + airflow.z * airflow.z;
    if (airSpeed2 <= 0.00000001f) return (Vector3){0};
    Vector3 relative = {airflow.x - velocity.x, airflow.y - velocity.y,
                        airflow.z - velocity.z};
    float speed = sqrtf(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
    const float airDensityKgM3 = 1.225f, dragCoefficient = 1.2f;
    const float projectedAreaM2 = kNatureWindSampleHeight * 0.012f;
    float drag = 0.5f * airDensityKgM3 * dragCoefficient * projectedAreaM2;
    float forceScale = drag * speed;
    if (dt > 0.0f) {
        // Backward-Euler quadratic drag impulse: never overshoot target air
        // velocity, even for a strong target field or a light canopy element.
        float speedScale = 2.0f / (1.0f + sqrtf(1.0f + 4.0f * drag * speed * dt / kNatureCanopyMassKg));
        forceScale = kNatureCanopyMassKg * (1.0f - speedScale) / dt;
    }
    return (Vector3){relative.x * forceScale, relative.y * forceScale,
                     relative.z * forceScale};
}

static void Nature_AddAnchoredMotion(void)
{
    static int s_natureAwakeMinX = 0, s_natureAwakeMaxX = NATURE_INTERACTION_RESOLUTION - 1;
    static int s_natureAwakeMinY = 0, s_natureAwakeMaxY = NATURE_INTERACTION_RESOLUTION - 1;
    Vector3 minimum, maximum;
    bool fieldsActive = MotionFields_GetAnchoredBounds(MOTION_RECEIVER_FOLIAGE, &minimum, &maximum);
    if (!fieldsActive && !s_natureForceAwake)
        return;
    float cellSize = kNatureInteractionWorldSize / NATURE_INTERACTION_RESOLUTION;
    float halfSize = kNatureInteractionWorldSize * 0.5f;
    bool awake = false;
    float maxBend = 0.0f;
    int queryCells = 0;
    int steps = (int)ceilf(s_natureForceDt * 120.0f);
    if (steps < 1) steps = 1;
    float step = s_natureForceDt / steps;

    // AABB Bounds Culling: compute grid coordinate limits for active motion fields and awake cells
    int gridMinX = 0, gridMaxX = NATURE_INTERACTION_RESOLUTION - 1;
    int gridMinY = 0, gridMaxY = NATURE_INTERACTION_RESOLUTION - 1;
    if (fieldsActive && !s_natureForceAwake) {
        float fMinX = minimum.x - kNatureInteractionMaxBend;
        float fMaxX = maximum.x + kNatureInteractionMaxBend;
        float fMinZ = minimum.z - kNatureInteractionMaxBend;
        float fMaxZ = maximum.z + kNatureInteractionMaxBend;
        gridMinX = (int)floorf((fMinX - (s_natureInteractionCenter.x - halfSize)) / cellSize);
        gridMaxX = (int)ceilf((fMaxX - (s_natureInteractionCenter.x - halfSize)) / cellSize);
        gridMinY = (int)floorf((fMinZ - (s_natureInteractionCenter.y - halfSize)) / cellSize);
        gridMaxY = (int)ceilf((fMaxZ - (s_natureInteractionCenter.y - halfSize)) / cellSize);
        if (gridMinX < 0) gridMinX = 0;
        if (gridMinY < 0) gridMinY = 0;
        if (gridMaxX >= NATURE_INTERACTION_RESOLUTION) gridMaxX = NATURE_INTERACTION_RESOLUTION - 1;
        if (gridMaxY >= NATURE_INTERACTION_RESOLUTION) gridMaxY = NATURE_INTERACTION_RESOLUTION - 1;
        if (gridMinX > gridMaxX || gridMinY > gridMaxY)
            return;
    } else if (!fieldsActive && s_natureForceAwake) {
        gridMinX = s_natureAwakeMinX;
        gridMaxX = s_natureAwakeMaxX;
        gridMinY = s_natureAwakeMinY;
        gridMaxY = s_natureAwakeMaxY;
        if (gridMinX > gridMaxX || gridMinY > gridMaxY) {
            s_natureForceAwake = false;
            return;
        }
    } else if (fieldsActive && s_natureForceAwake) {
        float fMinX = minimum.x - kNatureInteractionMaxBend;
        float fMaxX = maximum.x + kNatureInteractionMaxBend;
        float fMinZ = minimum.z - kNatureInteractionMaxBend;
        float fMaxZ = maximum.z + kNatureInteractionMaxBend;
        int fGridMinX = (int)floorf((fMinX - (s_natureInteractionCenter.x - halfSize)) / cellSize);
        int fGridMaxX = (int)ceilf((fMaxX - (s_natureInteractionCenter.x - halfSize)) / cellSize);
        int fGridMinY = (int)floorf((fMinZ - (s_natureInteractionCenter.y - halfSize)) / cellSize);
        int fGridMaxY = (int)ceilf((fMaxZ - (s_natureInteractionCenter.y - halfSize)) / cellSize);
        gridMinX = fminf(fGridMinX, s_natureAwakeMinX);
        gridMaxX = fmaxf(fGridMaxX, s_natureAwakeMaxX);
        gridMinY = fminf(fGridMinY, s_natureAwakeMinY);
        gridMaxY = fmaxf(fGridMaxY, s_natureAwakeMaxY);
        if (gridMinX < 0) gridMinX = 0;
        if (gridMinY < 0) gridMinY = 0;
        if (gridMaxX >= NATURE_INTERACTION_RESOLUTION) gridMaxX = NATURE_INTERACTION_RESOLUTION - 1;
        if (gridMaxY >= NATURE_INTERACTION_RESOLUTION) gridMaxY = NATURE_INTERACTION_RESOLUTION - 1;
        if (gridMinX > gridMaxX || gridMinY > gridMaxY)
            return;
    }

    int nextAwakeMinX = NATURE_INTERACTION_RESOLUTION - 1, nextAwakeMaxX = 0;
    int nextAwakeMinY = NATURE_INTERACTION_RESOLUTION - 1, nextAwakeMaxY = 0;

    for (int y = gridMinY; y <= gridMaxY; y++) {
        for (int x = gridMinX; x <= gridMaxX; x++) {
            int index = y * NATURE_INTERACTION_RESOLUTION + x;
            Vector2 *bend = &s_natureForceBend[index];
            Vector2 *velocity = &s_natureForceVelocity[index];
            Vector3 root = {
                s_natureInteractionCenter.x - halfSize + (x + 0.5f) * cellSize,
                0.0f,
                s_natureInteractionCenter.y - halfSize + (y + 0.5f) * cellSize,
            };
            bool inBounds = fieldsActive && root.x >= minimum.x - kNatureInteractionMaxBend &&
                root.x <= maximum.x + kNatureInteractionMaxBend &&
                root.z >= minimum.z - kNatureInteractionMaxBend &&
                root.z <= maximum.z + kNatureInteractionMaxBend;
            float stateSize = fabsf(bend->x) + fabsf(bend->y) + fabsf(velocity->x) + fabsf(velocity->y);
            if (!inBounds && stateSize < 0.001f) {
                *bend = (Vector2){0}; *velocity = (Vector2){0};
                continue;
            }
            if (inBounds) queryCells++;
            float groundY = MapManager_GetGroundHeightAt(root.x, root.z);
            if (!isfinite(groundY)) continue;
            root.y = groundY + kNatureWindSampleHeight;
            for (int substep = 0; substep < steps; substep++) {
                FieldSample sample = {0};
                if (inBounds) {
                    Vector3 position = {root.x + bend->x, root.y, root.z + bend->y};
                    Vector3 tipVelocity = {velocity->x, 0.0f, velocity->y};
                    BodyPhysicalProperties body = {
                        .massKg = kNatureCanopyMassKg,
                        .projectedAreaM2 = kNatureWindSampleHeight * 0.012f,
                        .dragCoefficient = 1.2f,
                    };
                    /* Ordinary Wind already drives the separate ambient
                     * receiver; this medium contains only direct field flow. */
                    MediumProperties medium = {.densityKgM3 = 1.225f,
                                               .gravityMps2 = {0, -9.81f, 0}};
                    ReceiverConstraints constraints = {.mode = RECEIVER_ROOTED,
                                                       .permittedAxes = {1, 0, 1}};
                    MotionFields_SampleBody(position, tipVelocity, &body, &medium,
                        &constraints, step, MOTION_RECEIVER_FOLIAGE, NULL, &sample);
                }
                Vector3 tipVelocity = {velocity->x, 0.0f, velocity->y};
                Vector3 airForce = sample.hasDragForce ? (Vector3){0} :
                    Nature_AnchoredAirForce(tipVelocity, sample.mediumVelocityMps, step);
                sample.forceNewtons.x += airForce.x + sample.accelerationMps2.x * kNatureCanopyMassKg;
                sample.forceNewtons.z += airForce.z + sample.accelerationMps2.z * kNatureCanopyMassKg;
                Nature_AdvanceAnchoredCell(bend, velocity, sample.forceNewtons, step);
            }
            maxBend = fmaxf(maxBend, sqrtf(bend->x * bend->x + bend->y * bend->y));
            bool cellAwake = fabsf(bend->x) + fabsf(bend->y) + fabsf(velocity->x) + fabsf(velocity->y) >= 0.001f;
            if (cellAwake) {
                awake = true;
                if (x < nextAwakeMinX) nextAwakeMinX = x;
                if (x > nextAwakeMaxX) nextAwakeMaxX = x;
                if (y < nextAwakeMinY) nextAwakeMinY = y;
                if (y > nextAwakeMaxY) nextAwakeMaxY = y;
            }
            Vector2 windBend = Nature_DecodeInteractionPixel(s_natureWindPixels[index]);
            s_natureWindPixels[index] = Nature_EncodeInteractionPixel((Vector2){
                windBend.x + bend->x, windBend.y + bend->y,
            });
        }
    }
    s_natureForceAwake = awake;
    if (awake) {
        s_natureAwakeMinX = nextAwakeMinX;
        s_natureAwakeMaxX = nextAwakeMaxX;
        s_natureAwakeMinY = nextAwakeMinY;
        s_natureAwakeMaxY = nextAwakeMaxY;
    } else {
        s_natureAwakeMinX = 0; s_natureAwakeMaxX = NATURE_INTERACTION_RESOLUTION - 1;
        s_natureAwakeMinY = 0; s_natureAwakeMaxY = NATURE_INTERACTION_RESOLUTION - 1;
    }
    if (Nature_WindTraceEnabled()) {
        static float traceElapsed = 0.5f;
        traceElapsed += s_natureForceDt;
        if (traceElapsed >= 0.5f) {
            TraceLog(LOG_INFO, "[MOTION] rooted_canopy fields=%d cells=%d max_tip_bend_m=%.3f awake=%d",
                     fieldsActive, queryCells, maxBend, awake);
            traceElapsed = 0.0f;
        }
    }
}

void MapProp_AddNatureWindVorticles(float time)
{
    if (!s_natureInteractionOpen)
        return;

    s_natureWindMacro = Wind_GetMacro();
    s_natureWindReceiverReady = true;
    double cacheStart = GetTime();
    Nature_UpdateMacroCache(time);
    if (Nature_WindTraceEnabled()) {
        static double totalMs = 0.0;
        static int samples = 0;
        totalMs += (GetTime() - cacheStart) * 1000.0;
        if (++samples == 60) {
            TraceLog(LOG_INFO, "NATURE_MACRO_CACHE: enabled=%d samples=%d avg_cpu_ms=%.3f resolution=%d world_size=%.1f",
                     s_natureMacroReady, samples, totalMs / samples,
                     NATURE_MACRO_RESOLUTION, kNatureMacroWorldSize);
            totalMs = 0.0;
            samples = 0;
        }
    }

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

        float vRadSq = vorticle->radius * vorticle->radius;
        for (int y = minY; y <= maxY; y++) {
            float sampleZ = fieldMinZ + ((float)y + 0.5f) * cellSize;
            float dz = sampleZ - vorticle->position.z;
            float dzSq = dz * dz;
            if (dzSq >= vRadSq) continue;

            for (int x = minX; x <= maxX; x++) {
                float sampleX = fieldMinX + ((float)x + 0.5f) * cellSize;
                float dx = sampleX - vorticle->position.x;
                if (dx * dx + dzSq >= vRadSq) continue;

                Vector3 samplePosition = { sampleX, 0.0f, sampleZ };
                float groundY = MapManager_GetGroundHeightAt(samplePosition.x,
                                                               samplePosition.z);
                if (!isfinite(groundY))
                    continue;
                samplePosition.y = groundY + kNatureWindSampleHeight;
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
    Nature_AddAnchoredMotion();
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
    // Reuse only byte-identical quantized texels. Wind/interaction evaluation
    // and shader region uniforms still advance normally on every frame.
    if (!s_natureInteractionUploadValid ||
        memcmp(s_natureInteractionUploaded, s_natureInteractionScratch,
               sizeof(s_natureInteractionScratch)) != 0) {
        UpdateTexture(s_natureInteractionTexture, s_natureInteractionScratch);
        memcpy(s_natureInteractionUploaded, s_natureInteractionScratch,
               sizeof(s_natureInteractionScratch));
        s_natureInteractionUploadValid = true;
    }
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
    s_natureInteractionUploadValid = false;
    if (s_natureMacroTexture.id != 0)
        UnloadTexture(s_natureMacroTexture);
    s_natureMacroTexture = (Texture2D){0};
    s_natureMacroReady = false;
    s_natureMacroVerified = false;
    s_natureMacroRegion = (Vector4){0};
    s_natureInteractionCenter = (Vector2){0};
    s_natureInteractionReady = false;
    memset(s_natureForceBend, 0, sizeof(s_natureForceBend));
    memset(s_natureForceVelocity, 0, sizeof(s_natureForceVelocity));
    s_natureForceAwake = false;
    s_natureForceDt = 0.0f;
    s_natureInteractionOpen = false;
    s_natureWindReceiverReady = false;
    s_natureWindMacro = (WindMacroConfig){0};
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
    MapProp_SetGrassState(MAP_GRASS_STATE_LUSH, 0.0f);
    MapProp_ClearGrassBurnZones();
}

#define NATURE_MAX_BURN_ZONES 8
static MapGrassCondition s_natureGrassCondition = {0.0f, 0.0f, 0.0f, 0.65f};
static Vector4 s_natureBurnZones[NATURE_MAX_BURN_ZONES] = {0};
static int s_natureBurnZoneCount = 0;

static MapGrassCondition Nature_ResolveGrassPreset(MapGrassState state, float emberIntensity)
{
    switch (state) {
        case MAP_GRASS_STATE_WITHERED:
            return (MapGrassCondition){
                .dryness = 0.50f,
                .scorch = 0.0f,
                .emberIntensity = 0.0f,
                .patchVariation = 0.72f
            };
        case MAP_GRASS_STATE_DRY:
            return (MapGrassCondition){
                .dryness = 0.96f,
                .scorch = 0.0f,
                .emberIntensity = 0.0f,
                .patchVariation = 0.60f
            };
        case MAP_GRASS_STATE_SCORCHED:
            return (MapGrassCondition){
                .dryness = 1.0f,
                .scorch = 0.92f,
                .emberIntensity = emberIntensity > 0.0f ? emberIntensity : 0.85f,
                .patchVariation = 0.78f
            };
        case MAP_GRASS_STATE_LUSH:
        default:
            return (MapGrassCondition){
                .dryness = 0.0f,
                .scorch = 0.0f,
                .emberIntensity = 0.0f,
                .patchVariation = 0.65f
            };
    }
}

void MapProp_SetGrassState(MapGrassState state, float emberIntensity)
{
    s_natureGrassCondition = Nature_ResolveGrassPreset(state, emberIntensity);
}

void MapProp_SetGrassCondition(MapGrassCondition condition)
{
    condition.dryness = fminf(fmaxf(condition.dryness, 0.0f), 1.0f);
    condition.scorch = fminf(fmaxf(condition.scorch, 0.0f), 1.0f);
    condition.emberIntensity = fminf(fmaxf(condition.emberIntensity, 0.0f), 2.5f);
    condition.patchVariation = fminf(fmaxf(condition.patchVariation, 0.0f), 1.0f);
    s_natureGrassCondition = condition;
}

MapGrassCondition MapProp_GetGrassCondition(void)
{
    return s_natureGrassCondition;
}

void MapProp_ClearGrassBurnZones(void)
{
    s_natureBurnZoneCount = 0;
    memset(s_natureBurnZones, 0, sizeof(s_natureBurnZones));
}

void MapProp_AddGrassBurnZone(Vector3 position, float radius, float scorchStrength)
{
    if (radius <= 0.05f || scorchStrength <= 0.001f)
        return;
    scorchStrength = fminf(fmaxf(scorchStrength, 0.0f), 1.5f);
    // Merge with an existing nearby burn zone if close enough
    for (int i = 0; i < s_natureBurnZoneCount; i++) {
        float dx = s_natureBurnZones[i].x - position.x;
        float dz = s_natureBurnZones[i].y - position.z;
        if (dx * dx + dz * dz < (radius * 0.35f) * (radius * 0.35f)) {
            s_natureBurnZones[i].z = fmaxf(s_natureBurnZones[i].z, radius);
            s_natureBurnZones[i].w = fmaxf(s_natureBurnZones[i].w, scorchStrength);
            return;
        }
    }
    if (s_natureBurnZoneCount < NATURE_MAX_BURN_ZONES) {
        s_natureBurnZones[s_natureBurnZoneCount++] =
            (Vector4){position.x, position.z, radius, scorchStrength};
    } else {
        // Replace the smallest / weakest burn zone
        int replaceIdx = 0;
        float minWeight = s_natureBurnZones[0].z * s_natureBurnZones[0].w;
        for (int i = 1; i < NATURE_MAX_BURN_ZONES; i++) {
            float w = s_natureBurnZones[i].z * s_natureBurnZones[i].w;
            if (w < minWeight) {
                minWeight = w;
                replaceIdx = i;
            }
        }
        s_natureBurnZones[replaceIdx] =
            (Vector4){position.x, position.z, radius, scorchStrength};
    }
}

static void Nature_UploadGrassStateUniforms(Shader shader)
{
    Vector4 stateVec = {
        s_natureGrassCondition.dryness,
        s_natureGrassCondition.scorch,
        s_natureGrassCondition.emberIntensity,
        s_natureGrassCondition.patchVariation
    };
    int stateLoc = GetShaderLocation(shader, "u_grassStateParams");
    if (stateLoc >= 0)
        SetShaderValue(shader, stateLoc, &stateVec, SHADER_UNIFORM_VEC4);
    int burnCountLoc = GetShaderLocation(shader, "u_grassBurnZoneCount");
    if (burnCountLoc >= 0)
        SetShaderValue(shader, burnCountLoc, &s_natureBurnZoneCount, SHADER_UNIFORM_INT);
    if (s_natureBurnZoneCount > 0) {
        int burnZonesLoc = GetShaderLocation(shader, "u_grassBurnZones");
        if (burnZonesLoc >= 0)
            SetShaderValueV(shader, burnZonesLoc, s_natureBurnZones,
                            SHADER_UNIFORM_VEC4, s_natureBurnZoneCount);
    }
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
        s_waterLocLakeRadii = GetShaderLocation(s_waterShader, "u_lakeRadii");
        s_waterLocLakeSeed = GetShaderLocation(s_waterShader, "u_lakeSeed");

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
                                         NatureWindResponseKind responseKind, float compliance)
{
    WindMacroConfig macro = s_natureWindMacro;
    int cacheEnabled = s_natureMacroReady && s_natureWindReceiverReady &&
                       responseKind == NATURE_WIND_RESPONSE_GRASS && compliance <= 0.06f ? 1 : 0;
    SetShaderValue(shader, GetShaderLocation(shader, "u_windCacheEnabled"),
                   &cacheEnabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windCacheRegion"),
                   &s_natureMacroRegion, SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windCacheScale"),
                   &s_natureMacroScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windCacheTimeRange"),
                   &s_natureMacroTimeRange, SHADER_UNIFORM_VEC2);
    if (cacheEnabled)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_windCacheMap"),
                              s_natureMacroTexture);
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
    Environment_BindCloudShadowShader(shader);
    float windLength = sqrtf(windDirection.x * windDirection.x + windDirection.y * windDirection.y);
    if (windLength > 0.0001f) {
        windDirection.x /= windLength;
        windDirection.y /= windLength;
    }
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    Vector4 sun = ColorNormalize(Environment_GetSunColor());
    Vector4 ambient = ColorNormalize(Environment_GetAmbientColor());
    Vector3 sunRgb = Vector3Scale((Vector3){sun.x, sun.y, sun.z}, Environment_GetSunIntensity());
    Vector3 ambientRgb = {ambient.x, ambient.y, ambient.z};
    Matrix worldFromShaderSpace = MatrixInvert(rlGetMatrixTransform());
    int worldFromShaderSpaceLoc = GetShaderLocation(shader, "u_worldFromShaderSpace");
    if (worldFromShaderSpaceLoc >= 0)
        SetShaderValueMatrix(shader, worldFromShaderSpaceLoc, worldFromShaderSpace);
    SetShaderValue(shader, GetShaderLocation(shader, "u_time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "u_windStrength"), &windStrength, SHADER_UNIFORM_FLOAT);
    Nature_UpdateWindFieldShader(shader, windDirection, responseKind, windStrength);
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
    Nature_UploadGrassStateUniforms(shader);
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
        // Expanded meshes recover world space through the inverse transform;
        // parametric tufts already use their explicit world offset.
        int worldOffsetLoc = GetShaderLocation(shader, "u_worldOffset");
        bool uniformsValid = (worldFromShaderSpaceLoc >= 0 || worldOffsetLoc >= 0) &&
                             impactEnabledLoc >= 0 && impactCenterLoc >= 0 &&
                             impactDirectionLoc >= 0 && impactTypeLoc >= 0 &&
                             impactRadiusLoc >= 0 &&
                             impactStrengthLoc >= 0 && impactAgeLoc >= 0;
        TraceLog(uniformsValid ? LOG_INFO : LOG_WARNING,
                 "[WIND_TRACE] vegetation_shader pass=visible shader=%u uniforms=%s enabled=%d type=%d center=(%.2f,%.2f) radius=%.2f bend=%.3f age=%.3f locations=(world:%d offset:%d enabled:%d center:%d direction:%d type:%d radius:%d strength:%d age:%d)",
                 shader.id, uniformsValid ? "ok" : "MISSING", windImpactEnabled,
                 s_natureWindImpactType,
                 s_natureWindImpactCenter.x, s_natureWindImpactCenter.y,
                 s_natureWindImpactRadius, s_natureWindImpactStrength,
                 s_natureWindImpactAge, worldFromShaderSpaceLoc, worldOffsetLoc,
                 impactEnabledLoc, impactCenterLoc, impactDirectionLoc,
                 impactTypeLoc, impactRadiusLoc, impactStrengthLoc, impactAgeLoc);
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
    Nature_UpdateWindFieldShader(shader, windDirection, responseKind, windStrength);
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
        // Expanded meshes recover world space through the inverse transform;
        // parametric tufts already use their explicit world offset.
        int worldOffsetLoc = GetShaderLocation(shader, "u_worldOffset");
        bool uniformsValid = (worldFromShaderSpaceLoc >= 0 || worldOffsetLoc >= 0) &&
                             impactEnabledLoc >= 0 && impactCenterLoc >= 0 &&
                             impactDirectionLoc >= 0 && impactTypeLoc >= 0 &&
                             impactRadiusLoc >= 0 &&
                             impactStrengthLoc >= 0 && impactAgeLoc >= 0;
        TraceLog(uniformsValid ? LOG_INFO : LOG_WARNING,
                 "[WIND_TRACE] vegetation_shader pass=shadow shader=%u uniforms=%s enabled=%d type=%d center=(%.2f,%.2f) radius=%.2f bend=%.3f age=%.3f locations=(world:%d offset:%d enabled:%d center:%d direction:%d type:%d radius:%d strength:%d age:%d)",
                 shader.id, uniformsValid ? "ok" : "MISSING", windImpactEnabled,
                 s_natureWindImpactType,
                 s_natureWindImpactCenter.x, s_natureWindImpactCenter.y,
                 s_natureWindImpactRadius, s_natureWindImpactStrength,
                 s_natureWindImpactAge, worldFromShaderSpaceLoc, worldOffsetLoc,
                 impactEnabledLoc, impactCenterLoc, impactDirectionLoc,
                 impactTypeLoc, impactRadiusLoc, impactStrengthLoc, impactAgeLoc);
        s_natureWindTraceShadowUploaded = true;
    }
    if (s_natureInteractionReady)
        SetShaderValueTexture(shader, GetShaderLocation(shader, "u_interactionMap"),
                              s_natureInteractionTexture);
}


#include "maps/toolkit/map_props_meadow.inl"
#include "maps/toolkit/map_props_flowers.inl"
#include "maps/toolkit/map_props_water.inl"
