// ============================================================================
// vc_freeze_patch.inl — Primary VFX: Conformal Ground & Water Freeze Patch
// ============================================================================
// Terrain- and water-conforming radial frost/ice crust with Voronoi fractures,
// organic FBM frost fingers, hoarfrost rime, and specular ice sheen.
// Included once by core/composition/water/water.inl.
// ============================================================================

#define FREEZE_PATCH_POOL_SIZE 8
#define FREEZE_PATCH_GRID_N    24
#define FREEZE_PATCH_VERTS_N   (FREEZE_PATCH_GRID_N + 1)

typedef struct {
    bool active;
    Vector3 center;
    float maxRadius;
    float currentRadius;
    float age;
    float spreadTime;
    float holdTime;
    float fadeTime;
    float waterMask;
    Color iceColor;
    Color frostColor;
    float gridY[FREEZE_PATCH_VERTS_N * FREEZE_PATCH_VERTS_N];
    Vector3 gridNorm[FREEZE_PATCH_VERTS_N * FREEZE_PATCH_VERTS_N];
    unsigned char gridAlpha[FREEZE_PATCH_VERTS_N * FREEZE_PATCH_VERTS_N];
} FreezePatchSlot;

static FreezePatchSlot s_freezePatchPool[FREEZE_PATCH_POOL_SIZE] = {0};
static Shader s_freezePatchShader = {0};
static int s_fpLocCenter    = -1;
static int s_fpLocRadius    = -1;
static int s_fpLocMaxRadius = -1;
static int s_fpLocAlpha     = -1;
static int s_fpLocWaterMask = -1;
static int s_fpLocIceColor  = -1;
static int s_fpLocFrostColor= -1;
static int s_fpLocLightDir  = -1;
static int s_fpLocLightCol  = -1;
static int s_fpLocViewPos   = -1;

static void FreezePatch_InitShaderIfNeeded(void) {
    if (s_freezePatchShader.id != 0) return;
    s_freezePatchShader = ResourceManager_LoadShader(
        "core/shaders/freeze_patch.vs",
        "core/shaders/freeze_patch.fs");
    if (s_freezePatchShader.id == 0) return;
    s_fpLocCenter     = GetShaderLocation(s_freezePatchShader, "u_center");
    s_fpLocRadius     = GetShaderLocation(s_freezePatchShader, "u_radius");
    s_fpLocMaxRadius  = GetShaderLocation(s_freezePatchShader, "u_maxRadius");
    s_fpLocAlpha      = GetShaderLocation(s_freezePatchShader, "u_alpha");
    s_fpLocWaterMask  = GetShaderLocation(s_freezePatchShader, "u_waterMask");
    s_fpLocIceColor   = GetShaderLocation(s_freezePatchShader, "u_iceColor");
    s_fpLocFrostColor = GetShaderLocation(s_freezePatchShader, "u_frostColor");
    s_fpLocLightDir   = GetShaderLocation(s_freezePatchShader, "u_lightDir");
    s_fpLocLightCol   = GetShaderLocation(s_freezePatchShader, "u_lightColor");
    s_fpLocViewPos    = GetShaderLocation(s_freezePatchShader, "viewPos");
}

static float FreezePatch_SampleSurfaceY(float wx, float wz, float fallbackY, bool *outHasWater) {
    float groundY = MapManager_GetGroundHeightAt(wx, wz);
    if (!isfinite(groundY)) groundY = fallbackY;
    float waterY = 0.0f;
    float waterDepth = 0.0f;
    bool hasWater = MapManager_GetWaterInfoAt(wx, wz, &waterY, &waterDepth);
    if (outHasWater) *outHasWater = (hasWater && waterDepth > 0.02f);
    if (hasWater && waterDepth > 0.02f && waterY > groundY) {
        return waterY + 0.024f;
    }
    return groundY + 0.024f;
}

static void FreezePatch_BakeTerrainGrid(FreezePatchSlot *slot) {
    float extent = slot->maxRadius * 1.18f;
    float step = (2.0f * extent) / (float)FREEZE_PATCH_GRID_N;
    int waterHits = 0;

    for (int iz = 0; iz < FREEZE_PATCH_VERTS_N; ++iz) {
        float lz = -extent + (float)iz * step;
        float wz = slot->center.z + lz;
        for (int ix = 0; ix < FREEZE_PATCH_VERTS_N; ++ix) {
            float lx = -extent + (float)ix * step;
            float wx = slot->center.x + lx;
            int idx = iz * FREEZE_PATCH_VERTS_N + ix;
            bool isWater = false;
            float sy = FreezePatch_SampleSurfaceY(wx, wz, slot->center.y, &isWater);
            if (isWater) waterHits++;
            slot->gridY[idx] = sy;

            float d = sqrtf(lx * lx + lz * lz);
            float edgeT = d / fmaxf(extent, 0.01f);
            float a = (edgeT >= 1.0f) ? 0.0f : (edgeT <= 0.78f ? 1.0f : (1.0f - (edgeT - 0.78f) / 0.22f));
            slot->gridAlpha[idx] = (unsigned char)(a * 255.0f);
        }
    }

    slot->waterMask = (float)waterHits / (float)(FREEZE_PATCH_VERTS_N * FREEZE_PATCH_VERTS_N);

    for (int iz = 0; iz < FREEZE_PATCH_VERTS_N; ++iz) {
        int iz0 = (iz > 0) ? (iz - 1) : iz;
        int iz1 = (iz < FREEZE_PATCH_GRID_N) ? (iz + 1) : iz;
        for (int ix = 0; ix < FREEZE_PATCH_VERTS_N; ++ix) {
            int ix0 = (ix > 0) ? (ix - 1) : ix;
            int ix1 = (ix < FREEZE_PATCH_GRID_N) ? (ix + 1) : ix;
            float hL = slot->gridY[iz * FREEZE_PATCH_VERTS_N + ix0];
            float hR = slot->gridY[iz * FREEZE_PATCH_VERTS_N + ix1];
            float hD = slot->gridY[iz0 * FREEZE_PATCH_VERTS_N + ix];
            float hU = slot->gridY[iz1 * FREEZE_PATCH_VERTS_N + ix];
            float dx = (float)(ix1 - ix0) * step;
            float dz = (float)(iz1 - iz0) * step;
            Vector3 n = (Vector3){
                (dx > 0.0001f) ? (hL - hR) / dx : 0.0f,
                1.0f,
                (dz > 0.0001f) ? (hD - hU) / dz : 0.0f
            };
            slot->gridNorm[iz * FREEZE_PATCH_VERTS_N + ix] = Vector3Normalize(n);
        }
    }
}

int VFX_FreezePatch_Spawn(Vector3 center, float maxRadius,
                          float spreadTime, float holdTime, float fadeTime,
                          Color iceColor, Color frostColor) {
    int chosen = -1;
    float oldestAge = -1.0f;
    for (int i = 0; i < FREEZE_PATCH_POOL_SIZE; ++i) {
        if (!s_freezePatchPool[i].active) {
            chosen = i;
            break;
        }
        if (s_freezePatchPool[i].age > oldestAge) {
            oldestAge = s_freezePatchPool[i].age;
            chosen = i;
        }
    }
    if (chosen < 0) return -1;

    FreezePatchSlot *slot = &s_freezePatchPool[chosen];
    memset(slot, 0, sizeof(*slot));
    slot->active = true;
    slot->center = center;
    slot->maxRadius = fmaxf(maxRadius, 0.35f);
    slot->currentRadius = 0.05f;
    slot->age = 0.0f;
    slot->spreadTime = fmaxf(spreadTime, 0.05f);
    slot->holdTime = fmaxf(holdTime, 0.05f);
    slot->fadeTime = fmaxf(fadeTime, 0.05f);
    slot->iceColor = (iceColor.a == 0) ? (Color){105, 195, 240, 235} : iceColor;
    slot->frostColor = (frostColor.a == 0) ? (Color){225, 245, 255, 255} : frostColor;

    FreezePatch_BakeTerrainGrid(slot);
    return chosen;
}

void VC_FreezePatch_ClearAll(void) {
    memset(s_freezePatchPool, 0, sizeof(s_freezePatchPool));
}

int VC_FreezePatch_GetActiveCount(void) {
    int count = 0;
    for (int i = 0; i < FREEZE_PATCH_POOL_SIZE; ++i) {
        if (s_freezePatchPool[i].active) count++;
    }
    return count;
}

void VC_FreezePatch_Update(float dt) {
    for (int i = 0; i < FREEZE_PATCH_POOL_SIZE; ++i) {
        FreezePatchSlot *slot = &s_freezePatchPool[i];
        if (!slot->active) continue;
        slot->age += dt;
        float totalLife = slot->spreadTime + slot->holdTime + slot->fadeTime;
        if (slot->age >= totalLife) {
            slot->active = false;
            continue;
        }
        float tSpread = fminf(1.0f, slot->age / slot->spreadTime);
        float easeOut = 1.0f - (1.0f - tSpread) * (1.0f - tSpread) * (1.0f - tSpread);
        slot->currentRadius = 0.08f + (slot->maxRadius - 0.08f) * easeOut;
    }
}

void VC_FreezePatch_Draw3D(Camera3D camera) {
    int activeCount = VC_FreezePatch_GetActiveCount();
    if (activeCount <= 0) return;

    FreezePatch_InitShaderIfNeeded();
    if (s_freezePatchShader.id == 0) return;

    Vector3 sunDir = Environment_GetSunDirection();
    Color sunCol = Environment_GetSunColor();
    Vector3 lightDir = Vector3Normalize((Vector3){-sunDir.x, -sunDir.y, -sunDir.z});
    if (Vector3LengthSqr(lightDir) < 0.001f) {
        lightDir = Vector3Normalize((Vector3){0.45f, 0.85f, 0.35f});
    }
    float lightDirArr[3] = { lightDir.x, lightDir.y, lightDir.z };
    float lightColArr[3] = {
        (float)sunCol.r / 255.0f,
        (float)sunCol.g / 255.0f,
        (float)sunCol.b / 255.0f
    };
    float viewPosArr[3] = { camera.position.x, camera.position.y, camera.position.z };

    VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
    SkillManager_BeginShader(s_freezePatchShader);
    rlDisableBackfaceCulling();

    if (s_fpLocLightDir >= 0) SetShaderValue(s_freezePatchShader, s_fpLocLightDir, lightDirArr, SHADER_UNIFORM_VEC3);
    if (s_fpLocLightCol >= 0) SetShaderValue(s_freezePatchShader, s_fpLocLightCol, lightColArr, SHADER_UNIFORM_VEC3);
    if (s_fpLocViewPos >= 0)  SetShaderValue(s_freezePatchShader, s_fpLocViewPos, viewPosArr, SHADER_UNIFORM_VEC3);

    for (int i = 0; i < FREEZE_PATCH_POOL_SIZE; ++i) {
        const FreezePatchSlot *slot = &s_freezePatchPool[i];
        if (!slot->active) continue;

        float fadeStart = slot->spreadTime + slot->holdTime;
        float alpha = 1.0f;
        if (slot->age < 0.06f) {
            alpha = slot->age / 0.06f;
        } else if (slot->age > fadeStart) {
            alpha = 1.0f - fminf(1.0f, (slot->age - fadeStart) / slot->fadeTime);
        }
        if (alpha <= 0.005f) continue;

        float centerArr[3] = { slot->center.x, slot->center.y, slot->center.z };
        float iceArr[3] = {
            (float)slot->iceColor.r / 255.0f,
            (float)slot->iceColor.g / 255.0f,
            (float)slot->iceColor.b / 255.0f
        };
        float frostArr[3] = {
            (float)slot->frostColor.r / 255.0f,
            (float)slot->frostColor.g / 255.0f,
            (float)slot->frostColor.b / 255.0f
        };

        if (s_fpLocCenter >= 0)     SetShaderValue(s_freezePatchShader, s_fpLocCenter, centerArr, SHADER_UNIFORM_VEC3);
        if (s_fpLocRadius >= 0)     SetShaderValue(s_freezePatchShader, s_fpLocRadius, &slot->currentRadius, SHADER_UNIFORM_FLOAT);
        if (s_fpLocMaxRadius >= 0)  SetShaderValue(s_freezePatchShader, s_fpLocMaxRadius, &slot->maxRadius, SHADER_UNIFORM_FLOAT);
        if (s_fpLocAlpha >= 0)      SetShaderValue(s_freezePatchShader, s_fpLocAlpha, &alpha, SHADER_UNIFORM_FLOAT);
        if (s_fpLocWaterMask >= 0)  SetShaderValue(s_freezePatchShader, s_fpLocWaterMask, &slot->waterMask, SHADER_UNIFORM_FLOAT);
        if (s_fpLocIceColor >= 0)   SetShaderValue(s_freezePatchShader, s_fpLocIceColor, iceArr, SHADER_UNIFORM_VEC3);
        if (s_fpLocFrostColor >= 0) SetShaderValue(s_freezePatchShader, s_fpLocFrostColor, frostArr, SHADER_UNIFORM_VEC3);

        float extent = slot->maxRadius * 1.18f;
        float step = (2.0f * extent) / (float)FREEZE_PATCH_GRID_N;

        rlCheckRenderBatchLimit(FREEZE_PATCH_GRID_N * FREEZE_PATCH_GRID_N * 6);
        rlBegin(RL_TRIANGLES);
        for (int iz = 0; iz < FREEZE_PATCH_GRID_N; ++iz) {
            float wz0 = slot->center.z - extent + (float)iz * step;
            float wz1 = wz0 + step;
            for (int ix = 0; ix < FREEZE_PATCH_GRID_N; ++ix) {
                float wx0 = slot->center.x - extent + (float)ix * step;
                float wx1 = wx0 + step;

                int i00 = iz * FREEZE_PATCH_VERTS_N + ix;
                int i10 = i00 + 1;
                int i01 = i00 + FREEZE_PATCH_VERTS_N;
                int i11 = i01 + 1;

                // Triangle 1: (00, 01, 11)
                rlColor4ub(255, 255, 255, slot->gridAlpha[i00]);
                rlNormal3f(slot->gridNorm[i00].x, slot->gridNorm[i00].y, slot->gridNorm[i00].z);
                rlVertex3f(wx0, slot->gridY[i00], wz0);

                rlColor4ub(255, 255, 255, slot->gridAlpha[i01]);
                rlNormal3f(slot->gridNorm[i01].x, slot->gridNorm[i01].y, slot->gridNorm[i01].z);
                rlVertex3f(wx0, slot->gridY[i01], wz1);

                rlColor4ub(255, 255, 255, slot->gridAlpha[i11]);
                rlNormal3f(slot->gridNorm[i11].x, slot->gridNorm[i11].y, slot->gridNorm[i11].z);
                rlVertex3f(wx1, slot->gridY[i11], wz1);

                // Triangle 2: (00, 11, 10)
                rlColor4ub(255, 255, 255, slot->gridAlpha[i00]);
                rlNormal3f(slot->gridNorm[i00].x, slot->gridNorm[i00].y, slot->gridNorm[i00].z);
                rlVertex3f(wx0, slot->gridY[i00], wz0);

                rlColor4ub(255, 255, 255, slot->gridAlpha[i11]);
                rlNormal3f(slot->gridNorm[i11].x, slot->gridNorm[i11].y, slot->gridNorm[i11].z);
                rlVertex3f(wx1, slot->gridY[i11], wz1);

                rlColor4ub(255, 255, 255, slot->gridAlpha[i10]);
                rlNormal3f(slot->gridNorm[i10].x, slot->gridNorm[i10].y, slot->gridNorm[i10].z);
                rlVertex3f(wx1, slot->gridY[i10], wz0);
            }
        }
        rlEnd();
    }

    rlEnableBackfaceCulling();
    SkillManager_EndShader();
    VFXRender_EndDraw(&scope);
}
