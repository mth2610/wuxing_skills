// ── PRIMARY. VFX_ComposeFlare — outer radiance rings ───────────────────────
//
// CoreGlow is intentionally useful without ceremonial geometry: it owns the
// white core, bloom shoulder and star rays. This companion owns only the outer
// radiance rings, so a caller chooses whether a glow is a compact light source
// or a full spell flare.

static SkillCurve s_flareFade = {0};
static Texture2D s_flareRingTex = {0};
static bool s_flareInit = false;

static void Flare_BuildRingTexture(void)
{
    const int S = 128;
    Image img = GenImageColor(S, S, BLANK);
    for (int y = 0; y < S; ++y)
    {
        for (int x = 0; x < S; ++x)
        {
            float u = ((float)x + 0.5f) / (float)S * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / (float)S * 2.0f - 1.0f;
            float r2 = u * u + v * v;
            float alpha = 0.0f;
            if (r2 < 1.0f)
            {
                float r = sqrtf(r2);
                float edge = 1.0f - r2;
                float edge2 = edge * edge;
                float angle = atan2f(v, u);
                float ringMod = 0.88f + 0.07f * sinf(angle * 5.0f + 0.7f)
                                         + 0.04f * sinf(angle * 9.0f - 0.4f);
                if (ringMod < 0.72f) ringMod = 0.72f;
                if (ringMod > 1.0f) ringMod = 1.0f;
                float ringD = (r - 0.56f) / 0.085f;
                float ring = expf(-(ringD * ringD)) * edge * 0.62f * ringMod;
                float ghostD = (r - 0.72f) / 0.050f;
                float ghost = expf(-(ghostD * ghostD)) * edge2 * 0.12f
                            * (1.35f - ringMod);
                alpha = ring + ghost;
                if (alpha > 1.0f) alpha = 1.0f;
            }
            ImageDrawPixel(&img, x, y,
                           (Color){255, 255, 255, (unsigned char)(alpha * 255.0f)});
        }
    }
    s_flareRingTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_flareRingTex.id != 0)
        SetTextureFilter(s_flareRingTex, TEXTURE_FILTER_BILINEAR);
}

static void Flare_InitShared(void)
{
    if (s_flareInit) return;
    FloatCurve_AddStop(&s_flareFade, 0.00f, 1.00f);
    FloatCurve_AddStop(&s_flareFade, 0.60f, 0.80f);
    FloatCurve_AddStop(&s_flareFade, 1.00f, 0.00f);
    Flare_BuildRingTexture();
    s_flareInit = true;
}

void VFX_ComposeFlare(Vector3 center, VC_MaterialId mat, float radius,
                      float intensity01)
{
    Flare_InitShared();
    if (radius <= 0.0f) radius = 1.0f;
    if (intensity01 < 0.0f) intensity01 = 0.0f;
    if (intensity01 > 1.0f) intensity01 = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    SpawnParticle((ParticleConfig){
        .position = center,
        .radius = radius * (0.48f + 0.22f * intensity01),
        .lifetime = 0.16f,
        .colorStart = VC_WithAlpha(m->soft, (unsigned char)(55 + 105 * intensity01)),
        .colorEnd = VC_WithAlpha(m->soft, 0),
        .alphaCurve = &s_flareFade,
        .render.texture = s_flareRingTex,
        .render.blendMode = VFX_BLEND_PREMULTIPLIED,
        .render.unlit = 1,
    });
}
