// ── E5.1 — VFX_ComposeGlintSparkle ───────────────────────────────────────────
//
// Anisotropic star glints over a point cloud: the Elden Ring holy/faith
// signature, and per the spec NOTHING in the existing components does it.
//
// BUILT ON THE REBUILT FOUNDATION ONLY. Đợt E is a restructure and the smoke
// puff (F2) is the one component that has actually been rebuilt, so this is
// assembled the same way it is — F1 lit particles, the F1b blend law, colours
// from VFX_Material, motion from vc_motion.h. It deliberately does not touch
// aura_shell / ground_aura / smoke_energy / effect_material: those are the
// single-surface-plus-FBM architecture §0.1b diagnoses as the problem, and
// building a new component out of them would repeat the character-aura mistake.
//
// WHY PARTICLES RATHER THAN HAND-DRAWN QUADS. The spec describes per-glint
// twinkling driven by VC_Flicker01 each frame, which reads as "draw N quads
// yourself". Routing it through the particle system instead gets the blend law,
// the batching, the soft-depth handling and the pooling for free, and keeps the
// component from becoming another hand-rolled geometry path. The twinkle comes
// from short lifetimes on a fade curve rather than a per-frame alpha: a glint
// that fades in and out IS a spawn/death cycle.
//
// BLEND LAW (§F1b): a glint EMITS light, so it is additive and unlit. Running it
// through the lighting multiply would brown it out against a night sky.

#define GLINT_MAX_POINTS 24

static SkillCurve  s_glintFade = {0};
static Texture2D   s_glintTex  = {0};
static bool        s_glintInit = false;

// Live knobs — a sparkle is judged entirely by eye.
static float s_glintRate    = 1.0f;   // x on glints/sec
static float s_glintSize    = 1.0f;   // x on glint radius
static float s_glintPoints  = 14.0f;  // constellation size (<= GLINT_MAX_POINTS)

// A 4-point star, generated rather than loaded.
//
// E4's `glint_star_4pt` sheet does not exist yet, and the spec requires every
// E5 component to work without its texture. The stock particle texture is a
// round radial blob, which is the ONE thing a glint must not be — "anisotropic"
// is the whole point. So the fallback is not a worse sprite, it is this: a star
// built from two crossed anisotropic lobes, computed once into an image.
//
// Deliberately NOT a noise hash: ENGINE_LANDMINES.md §4, `fract(sin(...))` dies
// on Mali. This is closed-form and identical on every device.
static void Glint_BuildStarTexture(void)
{
    const int S = 64;
    Image img = GenImageColor(S, S, BLANK);
    for (int y = 0; y < S; y++)
    {
        for (int x = 0; x < S; x++)
        {
            float u = ((float)x + 0.5f) / S * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / S * 2.0f - 1.0f;
            float au = fabsf(u), av = fabsf(v);

            // Two perpendicular lobes: each is bright along its own axis and
            // falls off sharply across it. The product of a long falloff and a
            // narrow one is what makes a spike rather than a blob.
            float horiz = fmaxf(0.0f, 1.0f - au) * expf(-av * 14.0f);
            float vert  = fmaxf(0.0f, 1.0f - av) * expf(-au * 14.0f);
            // Small round core so the spikes have something to emanate from.
            float core  = expf(-(au * au + av * av) * 26.0f);

            float a = horiz + vert + core * 0.9f;
            if (a > 1.0f) a = 1.0f;
            unsigned char c = (unsigned char)(a * 255.0f);
            // White RGB: the element tint is applied per-particle, so one sheet
            // serves every material (same convention as the smoke atlas).
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255, c});
        }
    }
    s_glintTex = LoadTextureFromImage(img);
    UnloadImage(img);
    if (s_glintTex.id != 0)
        SetTextureFilter(s_glintTex, TEXTURE_FILTER_BILINEAR);
}

static void Glint_InitShared(void)
{
    if (s_glintInit) return;

    // Fast bloom, slow decay — a glint is a specular flash, not a puff. Peaking
    // almost immediately is what makes it read as a catch of light rather than
    // something being born.
    FloatCurve_AddStop(&s_glintFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_glintFade, 0.15f, 1.0f);
    FloatCurve_AddStop(&s_glintFade, 1.0f, 0.0f);

    // Prefer an authored sheet if E4 ever lands one; otherwise build the star.
    s_glintTex = ResourceManager_LoadTexture("assets/textures/glint_star_4pt.png");
    if (s_glintTex.id == 0)
        Glint_BuildStarTexture();

    Tuning_RegisterFloat("glint_rate", &s_glintRate, 1.0f);
    Tuning_RegisterFloat("glint_size", &s_glintSize, 1.0f);
    Tuning_RegisterFloat("glint_points", &s_glintPoints, 14.0f);

    s_glintInit = true;
}

// Fibonacci sphere — even coverage without the pole clustering that a naive
// lat/long loop gives. Same distribution the thunder-orb work used.
static Vector3 Glint_FibonacciPoint(int i, int n)
{
    const float GOLDEN = 2.39996323f;         // pi * (3 - sqrt(5))
    float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)n;
    float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
    float th = GOLDEN * (float)i;
    return (Vector3){ cosf(th) * r, y, sinf(th) * r };
}

// Continuous: call once per frame with a running `time`.
// `scale` is the cloud RADIUS in metres (1.0 ~ a body-sized sparkle field).
void VFX_ComposeGlintSparkle(Vector3 center, VC_MaterialId mat, float scale, float time)
{
    Glint_InitShared();
    if (scale <= 0.0f) scale = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);

    int pts = (int)s_glintPoints;
    if (pts < 1) pts = 1;
    if (pts > GLINT_MAX_POINTS) pts = GLINT_MAX_POINTS;

    // Spawn budget carried between frames, so the rate is framerate-independent
    // and a slow frame does not emit a burst.
    static float s_accum = 0.0f;
    s_accum += GetFrameTime() * (34.0f * s_glintRate);
    int spawn = (int)s_accum;
    if (spawn > 5) spawn = 5;          // clamp after a hitch
    s_accum -= (float)spawn;

    for (int s = 0; s < spawn; s++)
    {
        // Which point of the constellation twinkles now. Flicker picks it rather
        // than a plain counter so the sparkle never marches around the sphere in
        // a visible order.
        int idx = (int)(VC_Flicker01(time, (float)s * 3.7f) * (float)pts) % pts;
        Vector3 dir = Glint_FibonacciPoint(idx, pts);

        // Sit the glint slightly off the shell so the cloud has depth instead of
        // reading as a wireframe sphere.
        float rr = scale * Math_Mix(0.82f, 1.06f, Random01());
        Vector3 p = {
            center.x + dir.x * rr,
            center.y + dir.y * rr,
            center.z + dir.z * rr,
        };

        // VC_Pulse01 varies the size between glints so the field has a hierarchy
        // — a few big catches of light among many small ones.
        float pulse = VC_Pulse01(time + (float)idx * 0.7f, 3.1f);
        // Sized so the constellation reads at a body-scale cloud without the
        // stars merging into a blob. Verified in the NEW FX tab.
        float radius = (0.085f + 0.13f * pulse) * scale * s_glintSize;

        SpawnParticle((ParticleConfig){
            .position = p,
            // Barely drifting: a glint is a highlight on something, not a spark
            // thrown off it. Motion would turn it into an ember.
            .velocity = { dir.x * 0.10f, dir.y * 0.10f + 0.05f, dir.z * 0.10f },
            .radius   = radius,
            .lifetime = Math_Mix(0.22f, 0.46f, Random01()),
            // glow, not body: a glint is the hot specular colour of the element.
            .colorStart = VC_WithAlpha(m->glow, 255),
            .colorEnd   = VC_WithAlpha(m->soft, 0),
            .alphaCurve = &s_glintFade,
            .render.texture   = s_glintTex,
            .render.blendMode = VFX_BLEND_ADDITIVE,  // emits...
            .render.unlit     = 1,                   // ...so it skips the lighting multiply
            // The white-hot core. 4.5 is the value the GPU particle upgrades
            // test already uses, so both paths agree on what "glowing" means.
            .render.emissiveBoost = 4.5f,
            // A star has an orientation; leaving every one axis-aligned reads as
            // a repeated stamp. Static per glint — spinning a star looks like a
            // pinwheel, not a highlight.
            .rotation = Random01() * 2.0f * PI,
        });
    }
}
