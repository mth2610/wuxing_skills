// DRAGON QI — GPU-skinned animated 3D Asian Dragon VFX compositions & curve presets
// (shared foundation for Giáng Long Thập Bát Chưởng and spirit summons).

static int s_dragonQiAssetHandle = -1;

int VFX_DragonQi_GetAsset(void)
{
    if (s_dragonQiAssetHandle < 0 || !VFXModel_IsAssetValid(s_dragonQiAssetHandle)) {
        s_dragonQiAssetHandle = VFXModel_RegisterAsset("assets/vfx/models/dragon_v12_gold.glb");
    }
    return s_dragonQiAssetHandle;
}

int VFX_ComposeDragonQi(Vector3 origin, VC_MaterialId material)
{
    (void)material;
    int asset = VFX_DragonQi_GetAsset();
    if (asset < 0) return -1;

    VFXModelSpawnParams params = { 0 };
    params.position      = (Vector3){ origin.x, origin.y + 1.35f, origin.z };
    params.rotationEuler = (Vector3){ 0.0f, 0.45f, 0.0f };
    params.scale         = 0.52f;
    params.clipName      = "swim_s";
    params.loop          = true;
    params.animSpeed     = 1.2f;
    params.lifetime      = 0.0f;
    params.fadeInTime    = 0.25f;
    params.fadeOutTime   = 0.35f;
    params.style         = VFX_MODEL_STYLE_QI_SPIRIT;
    params.tint          = (Color){ 228, 192, 86, 255 };
    params.glowColor     = (Color){ 255, 242, 166, 255 };
    params.glowIntensity = 3.10f;
    params.opacity       = 0.80f;

    return VFXModel_Spawn(asset, &params);
}

void VFX_KillDragonQi(int handle)
{
    VFXModel_Kill(handle);
}

Vector3 VFX_DragonCurve_Evaluate(float s, void *userData)
{
    const VFX_DragonCurveSpec *spec = (const VFX_DragonCurveSpec *)userData;
    if (!spec) return (Vector3){ 0.0f, 1.2f, 0.0f };

    float sc = (spec->scale > 0.05f) ? spec->scale : 1.0f;
    Vector3 dir   = spec->dir;
    Vector3 right = spec->right;

    if (spec->kind == VFX_DRAGON_CURVE_ORBIT) {
        float rad = (spec->radius > 0.2f) ? spec->radius : (2.2f * sc);
        float ang = spec->phaseOffset + (s / fmaxf(rad, 0.25f)) * spec->waveSign;
        Vector3 p = {
            spec->origin.x + cosf(ang) * rad,
            spec->origin.y + sinf(ang * 2.0f) * spec->verAmp,
            spec->origin.z + sinf(ang) * rad
        };
        return p;
    }

    if (spec->kind == VFX_DRAGON_CURVE_RISING_ERUPT) {
        float rise = Clamp(s / 3.2f, 0.0f, 1.0f);
        float ang  = spec->phaseOffset + s * 1.15f * spec->waveSign;
        float rad  = spec->latAmp * (1.0f - 0.35f * rise);
        Vector3 p = Vector3Add(spec->origin, Vector3Scale(dir, fmaxf(s, 0.0f) * 0.72f));
        p = Vector3Add(p, Vector3Scale(right, sinf(ang) * rad));
        float gY = MapManager_GetGroundHeightAt(p.x, p.z);
        p.y = gY - 0.65f + s * 0.85f + cosf(ang) * spec->verAmp;
        return p;
    }

    if (spec->kind == VFX_DRAGON_CURVE_DIVE_SLAM) {
        /* Arcs down from high aerial origin toward target ground */
        float total = fmaxf(spec->range, 2.0f);
        float u = Clamp(s / total, -0.35f, 1.25f);
        float ang = spec->phaseOffset + s * 1.35f * spec->waveSign;
        float rad = spec->latAmp * (1.0f - 0.65f * Clamp(u, 0.0f, 1.0f));
        Vector3 p = Vector3Add(spec->origin, Vector3Scale(dir, s * 0.65f));
        p = Vector3Add(p, Vector3Scale(right, sinf(ang) * rad));
        float gY = MapManager_GetGroundHeightAt(p.x, p.z);
        float startY = spec->origin.y + 3.6f * sc;
        float endY   = gY + 0.35f;
        p.y = startY + (endY - startY) * Clamp(u, 0.0f, 1.0f);
        return p;
    }

    /* S_SURGE or HELIX */
    if (s < 0.0f) {
        float back = -s;
        float coilAngle = back * 0.85f * spec->waveSign + spec->phaseOffset;
        float coilRad   = fminf(back * 0.42f, 1.10f) * sc;
        Vector3 p = Vector3Add(spec->origin, Vector3Scale(dir, -back * 0.55f));
        p = Vector3Add(p, Vector3Scale(right, sinf(coilAngle) * coilRad));
        p.y = spec->groundY + 1.15f + (1.0f - cosf(coilAngle)) * 0.22f * sc;
        return p;
    }

    float ramp = Clamp(s / 1.6f, 0.0f, 1.0f);
    ramp = ramp * ramp * (3.0f - 2.0f * ramp);
    float freq  = (spec->kind == VFX_DRAGON_CURVE_HELIX) ? 1.25f : 0.92f;
    float phase = s * freq + spec->phaseOffset;
    float lat   = sinf(phase) * spec->latAmp * ramp * spec->waveSign;
    float ver   = ((spec->kind == VFX_DRAGON_CURVE_HELIX) ? cosf(phase) : cosf(phase - 0.7f)) * spec->verAmp * ramp;

    float impactRise = 0.0f;
    if (spec->impacted && s > spec->impactArc - 0.4f) {
        impactRise = fmaxf(0.0f, s - (spec->impactArc - 0.4f)) * 0.55f;
    }

    Vector3 p = Vector3Add(spec->origin, Vector3Scale(dir, s));
    p = Vector3Add(p, Vector3Scale(right, lat));
    float gY = MapManager_GetGroundHeightAt(p.x, p.z);
    p.y = gY + 1.22f + 0.16f * ramp + ver + impactRise;
    return p;
}

void VFX_ComposeDragonSlam(Vector3 impactPos, float scale, bool withGroundCrack)
{
    float sc = (scale > 0.1f) ? scale : 1.0f;
    Vector3 gp = { impactPos.x, MapManager_GetGroundHeightAt(impactPos.x, impactPos.z) + 0.04f, impactPos.z };
    VFX_ComposeFireballBurst(impactPos, VC_MAT_HOLY, 1.05f * sc, 0.92f);
    VFX_SpawnShockRing(gp, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_HOLY, 2.2f * sc, 0.34f);
    VFX_ComposeEmberBurst(impactPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_HOLY, 1.25f * sc, 0.95f);
    if (withGroundCrack) {
        VFX_ComposeEarthCrackRadial(gp, 1.95f * sc, NULL);
    }
    Wind_SpawnRadialBlast(impactPos, 3.2f * sc, 8.5f, 0.22f);
    VFXLight_Spawn(impactPos, (Color){ 255, 240, 162, 255 }, 4.2f * sc, 0.35f, VFX_PRIORITY_HIGH_ULTIMATE);
}

void VFX_ComposePalmPrint(Vector3 pos, Vector3 normal, float radius, float duration)
{
    float r = (radius > 0.1f) ? radius : 1.4f;
    float d = (duration > 0.05f) ? duration : 0.32f;
    Vector3 n = (Vector3Length(normal) > 1e-4f) ? Vector3Normalize(normal) : (Vector3){ 0.0f, 1.0f, 0.0f };
    VFX_SpawnShockRing(pos, n, VC_MAT_HOLY, r, d);
    VFX_SpawnShockRing(pos, n, VC_MAT_HOLY, r * 0.58f, d * 0.78f);
    VFX_ComposeEmberBurst(pos, n, VC_MAT_HOLY, r * 0.62f, 0.85f);
    VFXLight_Spawn(pos, (Color){ 255, 242, 168, 255 }, r * 2.1f, d, VFX_PRIORITY_HIGH_ULTIMATE);
}
