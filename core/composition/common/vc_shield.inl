// Shield / Barrier Dome archetype — a defensive dome that scales in, holds,
// then either persists or fades (caller drives via `progress`: 0..~0.3 =
// rising, hold as long as needed, >0.85 = fading out, same "grow/hold/fade"
// convention as VFX_GroundPattern/VFX_ComposeAura).

void VFX_ComposeShield(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time)
{
    const VFX_ElementMaterial *elemMat = VFX_Material(matId);
    Color color = elemMat->body;
    const char *runePath = elemMat->runeDecal;

    // Grow-in over the first 0.3 of progress, fade-out over the last 0.15 —
    // caller can hold at any progress in between for as long as the shield
    // is meant to stay up (12.4 No Visual Popping: never appears at full size instantly).
    float growT = fminf(progress / 0.3f, 1.0f);
    float scaleIn = growT * growT * (3.0f - 2.0f * growT);
    float fadeAlpha = (progress > 0.85f) ? fmaxf(0.0f, (1.0f - progress) / 0.15f) : 1.0f;
    if (scaleIn <= 0.0f || fadeAlpha <= 0.0f)
        return;

    float domeRadius = radius * scaleIn;

    // Dome mesh: a full sphere centered a bit below ground so only the upper
    // cap is visible, reads as a hemisphere without a dedicated hemisphere mesh.
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    Vector3 domeCenter = {pos.x, pos.y, pos.z};

    // Subtle "impact breathe" — the barrier surface ripples gently as if
    // holding pressure, driven by time so it's never a dead static shell.
    float breathe = VC_Breathe(time, 2.4f, 0.015f);

    // Outer Fresnel shell — mostly see-through in the centre, energy-bright
    // at the grazing edge (the classic force-field silhouette).
    EffectMaterialParams p = {0};
    p.baseColor = ColorAlpha(color, 0.22f * fadeAlpha);
    p.rimStrength = 2.6f;
    p.fresnelPower = 3.2f;
    p.emissiveIntensity = 1.4f;
    p.distortionStrength = 0.15f;
    p.translucency = 0.9f;
    EffectMaterial mat;
    Material_LoadCustom(&mat, &p);
    Material_Begin(mat);
    DrawCoreSphere(domeCenter, domeRadius * breathe, 24, 24, WHITE);
    Material_End();

    // Inner faint fill — a slightly smaller, softer sphere gives the dome
    // volume/depth instead of a single hollow rim.
    EffectMaterialParams ip = {0};
    ip.baseColor = ColorAlpha(color, 0.12f * fadeAlpha);
    ip.rimStrength = 1.4f;
    ip.fresnelPower = 2.0f;
    ip.emissiveIntensity = 0.8f;
    ip.translucency = 0.8f;
    EffectMaterial imat;
    Material_LoadCustom(&imat, &ip);
    Material_Begin(imat);
    DrawCoreSphere(domeCenter, domeRadius * 0.92f, 18, 18, WHITE);
    Material_End();

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // Inner flow — energy bands sweeping up the dome surface (Halo-shield
    // read). Each band is a horizontal ring hugging the sphere at its
    // latitude: ring radius = sqrt(R² - y²). Band 0 is a persistent faint
    // climber; band 1 is the periodic bright WAVE that washes over the
    // surface every ~2.8s then rests.
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    for (int band = 0; band < 2; band++)
    {
        float cycle = (band == 0) ? fmodf(time * 0.35f, 1.0f)
                                  : fmodf(time / 2.8f, 1.0f) * 3.0f; // wave: sweeps in the first third, rests the rest
        if (cycle > 1.0f)
            continue;
        float bandY = cycle * domeRadius * 0.92f;
        float latR = sqrtf(fmaxf(domeRadius * domeRadius - bandY * bandY, 0.0001f));
        // Fade out as the band approaches the pole so it never pinches to a dot.
        float bandFade = (1.0f - cycle * cycle) * fadeAlpha * ((band == 0) ? 0.35f : 0.8f);

        EffectMaterialParams bp = {0};
        bp.baseColor = ColorAlpha(color, bandFade);
        bp.emissiveIntensity = (band == 0) ? 1.0f : 1.8f;
        EffectMaterial bmat;
        Material_LoadCustom(&bmat, &bp);
        Material_Begin(bmat);
        DrawCoreTorus((Vector3){domeCenter.x, domeCenter.y + bandY, domeCenter.z},
                      latR * 0.985f, latR * 1.005f, 4, 32, WHITE);
        Material_End();
    }
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    rlDrawRenderBatchActive();

    // Rotating hex/rune ring at the base — grounds the dome visually and
    // reads as the "generator" the barrier is projecting from.
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    Texture2D runeTex = ResourceManager_LoadTexture(runePath);
    Texture2D glowTex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
    unsigned char a = (unsigned char)(190 * fadeAlpha);

    // Element rune ring — spins slowly, the "generator" the barrier projects
    // from. A second, larger, counter-rotating soft glow ring underneath adds
    // depth and keeps the base from looking like a single flat sticker.
    VC_DrawGroundRune(glowTex, (Vector3){pos.x, pos.y + 0.005f, pos.z},
                      domeRadius * 1.15f, -time * 14.0f, VC_WithAlpha(color, (unsigned char)(a * 0.5f)));
    VC_DrawGroundRune(runeTex, (Vector3){pos.x, pos.y + 0.012f, pos.z},
                      domeRadius * 0.95f, time * 30.0f, VC_WithAlpha(color, a));

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

    // Orbit particles — slow motes circling the dome base, tangential
    // velocity so they visibly ride around the barrier's rim.
    if (GetRandomValue(0, 100) < 20)
    {
        float oa = Random01() * 2.0f * PI;
        float orbR = domeRadius * (0.95f + Random01() * 0.15f);
        Vector3 spawnPos = VC_RingPointXZ(pos, orbR, oa);
        spawnPos.y += 0.05f + Random01() * domeRadius * 0.25f;
        Vector3 tangent = VC_TangentXZ(oa, 0.08f);
        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = Vector3Scale(tangent, 0.35f + Random01() * 0.25f),
            .colorStart = ColorAlpha(color, 0.8f * fadeAlpha),
            .colorEnd = ColorAlpha(color, 0.0f),
            .radius = 0.012f + Random01() * 0.01f,
            .lifetime = 0.8f + Random01() * 0.5f});
    }

    // Occasional surface glint — reads as ambient energy crackling on the barrier.
    if (GetRandomValue(0, 100) < 8)
    {
        float yaw = ((float)GetRandomValue(0, 3600)) / 10.0f * DEG2RAD;
        float pitch = ((float)GetRandomValue(-300, 800)) / 10.0f * DEG2RAD; // biased upward (visible cap)
        Vector3 dir = {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)};
        Vector3 surfacePos = Vector3Add(domeCenter, Vector3Scale(dir, domeRadius));
        VFX_ComposeGlintBurst(surfacePos, 3, 0.08f, color);
    }
}
