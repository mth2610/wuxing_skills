// Melee Slash Arc archetype — a curved blade-trail swing, thin at both ends
// and widest at the peak, sweeping through `arcDegrees` around `pos` as
// `progress` goes 0..1. `dir` is the swing's starting facing (XZ-plane).

void VFX_ComposeSlashArc(VC_MaterialId matId, Vector3 pos, Vector3 dir, float radius, float arcDegrees, float progress, float time)
{
    Color color = VFX_Material(matId)->body;

    if (progress <= 0.0f || progress >= 1.0f)
        return;

    Vector3 flatDir = {dir.x, 0.0f, dir.z};
    if (Vector3Length(flatDir) < 0.001f)
        flatDir = (Vector3){0.0f, 0.0f, 1.0f};
    flatDir = Vector3Normalize(flatDir);
    float startAngle = atan2f(flatDir.z, flatDir.x) - (arcDegrees * 0.5f) * DEG2RAD;

    const int pointCount = 24;
    static RibbonPoint ribbonPoints[24];
    static RibbonPoint corePoints[24];
    static RibbonPoint ghostPoints[24];

    // Sweep only reveals up to `progress` of the arc — a partially-drawn
    // blade trail, not the whole arc popping in at once. Slight vertical bow
    // (peaks at the arc apex) so it reads as a swung blade, not a flat ring.
    for (int i = 0; i < pointCount; i++)
    {
        float norm = (float)i / (float)(pointCount - 1);
        float sweepT = norm * progress;
        float angle = startAngle + sweepT * arcDegrees * DEG2RAD;

        // Crescent profile: thin at BOTH ends, fattest at the arc's apex.
        float crescent = sinf(norm * PI);
        // Leading-edge emphasis so the current tip still reads bright/sharp
        // even though the crescent tapers it thin.
        float lead = 0.35f + 0.65f * norm;

        Vector3 p = {pos.x + cosf(angle) * radius,
                     pos.y + crescent * radius * 0.08f,
                     pos.z + sinf(angle) * radius};

        ribbonPoints[i].position = p;
        ribbonPoints[i].halfWidth = radius * 0.14f * crescent;
        ribbonPoints[i].v = norm;
        ribbonPoints[i].tint = ColorAlpha(color, (0.12f + 0.55f * crescent) * lead);

        // Bright inner core ribbon — thinner, whiter, sits inside the trail
        // and gives the swing a molten/hot cutting line.
        Color hot = {(unsigned char)(color.r + (255 - color.r) * 0.7f),
                     (unsigned char)(color.g + (255 - color.g) * 0.7f),
                     (unsigned char)(color.b + (255 - color.b) * 0.7f), 255};
        corePoints[i].position = p;
        corePoints[i].halfWidth = radius * 0.05f * crescent;
        corePoints[i].v = norm;
        corePoints[i].tint = ColorAlpha(hot, (0.2f + 0.7f * crescent) * lead);

        // Afterimage — the same sweep a few degrees behind, wider and much
        // fainter: the ghost the blade leaves hanging in the air.
        float ghostT = norm * fmaxf(progress - 0.08f, 0.0f);
        float ghostAngle = startAngle + ghostT * arcDegrees * DEG2RAD;
        ghostPoints[i].position = (Vector3){pos.x + cosf(ghostAngle) * radius * 0.98f,
                                            pos.y + crescent * radius * 0.06f,
                                            pos.z + sinf(ghostAngle) * radius * 0.98f};
        ghostPoints[i].halfWidth = radius * 0.2f * crescent;
        ghostPoints[i].v = norm;
        ghostPoints[i].tint = ColorAlpha(color, 0.1f * crescent);
    }

    EffectMaterialParams matParams = {0};
    matParams.baseColor = ColorAlpha(color, 0.8f);
    matParams.rimStrength = 2.0f;
    matParams.fresnelPower = 2.5f;
    matParams.emissiveIntensity = 1.8f;
    matParams.translucency = 0.5f;
    EffectMaterial mat = Material_LoadCustom(matParams);

    EffectMaterialParams coreMatParams = {0};
    coreMatParams.baseColor = ColorAlpha(WHITE, 0.9f);
    coreMatParams.emissiveIntensity = 2.6f;
    EffectMaterial coreMat = Material_LoadCustom(coreMatParams);

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    // Back-to-front: ghost afterimage, then body glow, then hot core.
    Material_Begin(mat);
    DrawRibbonStrip(ghostPoints, pointCount, (Texture2D){0}, camera);
    Material_End();

    Material_Begin(mat);
    DrawRibbonStrip(ribbonPoints, pointCount, (Texture2D){0}, camera);
    Material_End();

    Material_Begin(coreMat);
    DrawRibbonStrip(corePoints, pointCount, (Texture2D){0}, camera);
    Material_End();

    // Bright tip — an always-on hot bead at the exact cutting edge. This is
    // the single most important read of the swing: the eye locks onto the
    // point doing the damage.
    Vector3 tip = ribbonPoints[pointCount - 1].position;
    Material_Begin(coreMat);
    DrawCoreSphere(tip, radius * 0.045f, 10, 10, WHITE);
    Material_End();

    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();

    VFXLight_Spawn(tip, color, radius * 0.8f, 0.08f, VFX_PRIORITY_LOW);

    // Sparks shed off the cutting edge, thrown outward along the swing's
    // instantaneous direction (tangent), like grinding metal.
    if (GetRandomValue(0, 100) < 60)
    {
        float tipAngle = startAngle + progress * arcDegrees * DEG2RAD;
        Vector3 tangent = VC_TangentXZ(tipAngle, 0.15f);
        SpawnParticle((ParticleConfig){
            .position = tip,
            .velocity = Vector3Scale(tangent, (1.2f + Random01() * 0.8f) * (arcDegrees >= 0 ? 1.0f : -1.0f)),
            .colorStart = WHITE,
            .colorEnd = ColorAlpha(color, 0.0f),
            .radius = 0.01f + Random01() * 0.008f,
            .lifetime = 0.15f + Random01() * 0.1f});
    }
    if (GetRandomValue(0, 100) < 50)
        VFX_ComposeGlintBurst(tip, 3, 0.06f, color);

    // Air shear at the cutting edge — a whisper of distortion trailing the tip.
    if (GetRandomValue(0, 100) < 15)
        ScreenDistort_Add(tip, radius * 0.4f, 0.1f, 0.2f, 2.0f);
}
