// ── PRIMARY. VFX_ComposeCoreGlow — one hot point of light ───────────────────

#define CORE_GLOW_RATE_MIN 8.0f  // sprites/sec at intensity 0
#define CORE_GLOW_RATE_MAX 30.0f // ...and at intensity 1
#define CORE_GLOW_BATCH_MAX 4    // clamp after a frame hitch

static SkillCurve s_coreGlowFade;
static Texture2D s_coreGlowFlareTex = {0};
static bool s_coreGlowInit = false;

// x on the whole thing's size, and a kill switch for the point light. Both are
// look decisions, and the alternative to a tunable is a rebuild per guess.
static float s_coreGlowSize = 1.0f;
static float s_coreGlowLight = 1.0f;

// One camera-facing sheet carries the circular shoulder and every flare ray.
// Building those marks from separate stretched particles costs more pool slots,
// more overdraw, and lets their lifetimes drift out of symmetry. A dedicated
// shader would add a program/state switch for a profile that never changes.
// This 128x128 closed-form mask is generated once, uses one quad, and remains
// material-agnostic because RGB stays white and the particle supplies the tint.
static void CoreGlow_BuildFlareTexture(void)
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

                // The ring is broad and slightly uneven, like radiance moving
                // through haze rather than a geometry-perfect wire circle. The
                // second faint shoulder breaks the single stamped boundary.
                float angle = atan2f(v, u);
                float ringMod = 0.88f + 0.07f * sinf(angle * 5.0f + 0.7f)
                                         + 0.04f * sinf(angle * 9.0f - 0.4f);
                if (ringMod < 0.72f) ringMod = 0.72f;
                if (ringMod > 1.0f) ringMod = 1.0f;
                float halo = edge2 * edge2 * 0.24f;
                float ringD = (r - 0.56f) / 0.085f;
                float ring = expf(-(ringD * ringD)) * edge * 0.40f * ringMod;
                float ghostD = (r - 0.72f) / 0.050f;
                float ghost = expf(-(ghostD * ghostD)) * edge2 * 0.06f
                            * (1.35f - ringMod);

                // Long cardinal rays and shorter diagonal rays. max(), rather
                // than sum(), keeps each crossed pair from making a square core.
                float au = fabsf(u), av = fabsf(v);
                float horizontal = powf(fmaxf(0.0f, 1.0f - au), 1.10f)
                                 * expf(-av * 22.0f);
                float vertical = powf(fmaxf(0.0f, 1.0f - av), 1.25f)
                               * expf(-au * 24.0f);
                float cardinal = fmaxf(horizontal * 1.15f, vertical * 0.72f);

                const float INV_SQRT2 = 0.70710678f;
                float du = (u + v) * INV_SQRT2;
                float dv = (v - u) * INV_SQRT2;
                float adu = fabsf(du), adv = fabsf(dv);
                float diagonal = fmaxf(
                    powf(fmaxf(0.0f, 1.0f - adu / 0.92f), 1.35f) * expf(-adv * 26.0f),
                    powf(fmaxf(0.0f, 1.0f - adv / 0.92f), 1.35f) * expf(-adu * 26.0f));

                // Keep spokes from meeting as a mechanical hub. The compact
                // white particle owns the centre; these rays bloom outward.
                float cardT = (r - 0.08f) / 0.24f;
                if (cardT < 0.0f) cardT = 0.0f;
                if (cardT > 1.0f) cardT = 1.0f;
                float cardGate = 0.12f + 0.88f * cardT * cardT
                                             * (3.0f - 2.0f * cardT);
                float diagT = (r - 0.18f) / 0.20f;
                if (diagT < 0.0f) diagT = 0.0f;
                if (diagT > 1.0f) diagT = 1.0f;
                float diagGate = diagT * diagT * (3.0f - 2.0f * diagT);
                float core = expf(-r2 * 26.0f) * 0.72f;
                alpha = halo + ring + ghost
                      + edge2 * (cardinal * 2.10f * cardGate
                               + diagonal * 1.15f * diagGate)
                      + core;
                if (alpha > 1.0f) alpha = 1.0f;
            }

            ImageDrawPixel(&img, x, y,
                           (Color){255, 255, 255, (unsigned char)(alpha * 255.0f)});
        }
    }

    s_coreGlowFlareTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_coreGlowFlareTex.id != 0)
        SetTextureFilter(s_coreGlowFlareTex, TEXTURE_FILTER_BILINEAR);
}

static void CoreGlow_InitShared(void)
{
    if (s_coreGlowInit)
        return;
    // Hold, then fall. A linear fade on a sprite this bright reads as a blink;
    // the hold is what makes it a steady point rather than a flicker.
    FloatCurve_AddStop(&s_coreGlowFade, 0.00f, 1.00f);
    FloatCurve_AddStop(&s_coreGlowFade, 0.55f, 0.85f);
    FloatCurve_AddStop(&s_coreGlowFade, 1.00f, 0.00f);

    CoreGlow_BuildFlareTexture();

    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("coreglow_size", &s_coreGlowSize, 1.0f);
    Tuning_RegisterFloat("coreglow_light", &s_coreGlowLight, 1.0f);
    s_coreGlowInit = true;
}

void VFX_ComposeCoreGlow(Vector3 center, VC_MaterialId mat, float radius,
                         float intensity01)
{
    CoreGlow_InitShared();
    if (radius <= 0.0f)
        radius = 1.0f;
    if (intensity01 < 0.0f)
        intensity01 = 0.0f;
    if (intensity01 > 1.0f)
        intensity01 = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float i01 = intensity01;
    float scale = radius * s_coreGlowSize;

    // A RATE carried between frames, never a count per call — the same rule as
    // any per-frame emitter (VFX_PLAN §0.3). A count here would make the glow's
    // brightness a function of the frame rate.
    static float s_accum = 0.0f;
    s_accum += TimeFX_RawDelta() * Math_Mix(CORE_GLOW_RATE_MIN, CORE_GLOW_RATE_MAX, i01);
    int batch = (int)s_accum;
    if (batch > CORE_GLOW_BATCH_MAX)
        batch = CORE_GLOW_BATCH_MAX;
    s_accum -= (float)batch;
    if (batch <= 0)
        return;

    for (int c = 0; c < batch; c++)
    {
        // 1. THE HOT CORE. Grows only gently: additive brightness is energy
        // spread over AREA, so a core that scales fast gets DIMMER as it fills.
        SpawnParticle((ParticleConfig){
            .position = center,
            .radius = (0.045f + 0.075f * i01) * scale,
            .lifetime = Math_Mix(0.10f, 0.18f, Random01()),
            // Nearly white — the one place the effect is too bright to have a
            // colour. The element hue returns only as it fades.
            .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.75f), 255),
            .colorEnd = VC_WithAlpha(m->glow, 0),
            .alphaCurve = &s_coreGlowFade,
            .render.blendMode = VFX_BLEND_PREMULTIPLIED, // emits...
            .render.unlit = 1,                      // ...so no lighting multiply
            .render.emissiveBoost = Math_Mix(4.0f, 14.0f, i01),
        });
    }

    // 2 and 3 are the falloff AROUND the batch, not independent events, so they
    // fire once per batch rather than once per sprite. The batch itself is
    // rate-driven, so this stays framerate-independent — but it does mean their
    // density follows the core's rate, which is intended: a brighter core wants
    // a denser skirt.
    SpawnParticle((ParticleConfig){
        .position = center,
        .radius = (0.10f + 0.16f * i01) * scale,
        .lifetime = 0.13f,
        .colorStart = VC_WithAlpha(VC_Whiten(m->glow, 0.5f), 200),
        .colorEnd = VC_WithAlpha(m->glow, 0),
        .alphaCurve = &s_coreGlowFade,
        .render.blendMode = VFX_BLEND_PREMULTIPLIED,
        .render.unlit = 1,
        .render.emissiveBoost = Math_Mix(1.6f, 3.2f, i01),
    });

    SpawnParticle((ParticleConfig){
        .position = center,
        .radius = (0.22f + 0.30f * i01) * scale,
        .lifetime = 0.14f,
        .colorStart = VC_WithAlpha(m->soft, (unsigned char)(40 + 90 * i01)),
        .colorEnd = VC_WithAlpha(m->soft, 0),
        .alphaCurve = &s_coreGlowFade,
        .render.texture = s_coreGlowFlareTex,
        .render.blendMode = VFX_BLEND_PREMULTIPLIED,
        .render.unlit = 1,
        // NO boost. This is the glow around the hot spot; boosting it would make
        // it a second hot spot and the falloff would be gone.
    });

    // E2 point light, on its own timer. The pool is 16 slots and a 0.09 s light
    // spawned every frame at 60 fps would hold five of them for this one effect.
    if (s_coreGlowLight > 0.5f)
    {
        static float s_lightAccum = 0.0f;
        s_lightAccum += TimeFX_RawDelta();
        if (s_lightAccum >= 0.07f)
        {
            s_lightAccum = 0.0f;
            VFXLight_Spawn(center, m->soft, radius * Math_Mix(0.7f, 2.1f, i01),
                           0.09f, VFX_PRIORITY_LOW);
        }
    }
}
