/* REFERENCE PARTICLES — the calibration target for the PARTICLE path.
 *
 * REF BANDS proved the pipeline from the scene target onward by drawing quads
 * directly. It says nothing about particles, which reach that target through a
 * different route entirely: SpawnParticle -> the pool -> appearance resolution
 * -> batching -> the billboard shader -> the default sprite. Every one of those
 * can be wrong on its own.
 *
 * This was asked for after a real failure, and the failure is the reason the
 * file exists. A "before/after" pair of captures for a default-sprite change was
 * published and later measured BYTE-IDENTICAL — so the change had either not
 * reached particles or not been rebuilt, and there was no instrument that could
 * tell which. Art fixtures could not answer it: they animate, they use their own
 * textures, and their radiance is unknown by construction.
 *
 * So: a fixed number of particles, at fixed positions, with NO velocity, NO
 * gravity, NO RNG and a lifetime long enough to outlast any warmup — carrying
 * emissiveBoost values that are known by construction, and deliberately NOT
 * setting render.texture so that the DEFAULT sprite is what gets measured.
 *
 * WHAT IT LETS YOU ASSERT:
 *   1. a particle at boost B peaks at colour*B in the scene target
 *   2. the fraction of its footprint above the bloom threshold matches the
 *      sprite's own alpha profile — which is what makes a white core possible
 *      (BRIGHT_BACKGROUND_VFX_SPEC.md §7.6)
 *   3. GLOW and SOLID differ in the ONE way they are supposed to
 *   4. a change to the default sprite actually reaches particles at all
 */

#define REF_PARTICLE_COUNT 6

/* Spread across §7.6's bands: below the bloom threshold, astride it, and into
 * the range where a core can read as hot. */
static const float k_refParticleBoost[REF_PARTICLE_COUNT] = {
    0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 12.0f
};

static bool    s_refParticlesOn = false;
static Vector3 s_refParticlePos = {0};
static float   s_refParticleScale = 1.0f;
static float   s_refParticleTimer = 0.0f;

int VFX_ComposeRefParticles(Vector3 pos, float scale)
{
    s_refParticlePos = pos;
    s_refParticleScale = (scale > 0.0f) ? scale : 1.0f;
    s_refParticlesOn = true;
    s_refParticleTimer = 0.0f;
    TraceLog(LOG_INFO, "VFX_REF_PARTICLE: %d glow particles, boost %.1f .. %.1f, "
                       "plus one SOLID control — DEFAULT sprite",
             REF_PARTICLE_COUNT, k_refParticleBoost[0],
             k_refParticleBoost[REF_PARTICLE_COUNT - 1]);
    return 0;
}

void VFX_KillRefParticles(int id) { (void)id; s_refParticlesOn = false; }

void VC_RefParticles_Update(float dt)
{
    if (!s_refParticlesOn) return;
    /* Respawned on a slow cadence rather than every frame: one spawn per
     * particle would stack thousands of coincident billboards and the additive
     * sum, not the sprite, would be what got measured. The lifetime below
     * outlasts this interval, so exactly one generation is ever alive. */
    s_refParticleTimer -= dt;
    if (s_refParticleTimer > 0.0f) return;
    s_refParticleTimer = 4.0f;

    const float pitch = 0.75f * s_refParticleScale;
    for (int i = 0; i < REF_PARTICLE_COUNT; i++)
    {
        float off = ((float)i - (float)(REF_PARTICLE_COUNT - 1) * 0.5f) * pitch;
        SpawnParticle((ParticleConfig){
            .position = {s_refParticlePos.x + off,
                         s_refParticlePos.y + 0.45f * s_refParticleScale,
                         s_refParticlePos.z},
            .velocity = {0.0f, 0.0f, 0.0f},      /* stands still: measurable */
            .radius = 0.22f * s_refParticleScale,
            .lifetime = 6.0f,                    /* outlasts any warmup */
            .colorStart = WHITE,                 /* achromatic: value, not hue */
            .colorEnd = WHITE,
            /* THE GLOW CONTRACT, stated by hand rather than by preset: naming
               VFX_APPEARANCE_GLOW would DISCARD the per-particle boost below,
               because VFXAppearance_Resolve returns its table row wholesale. */
            .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1,
            .render.emissiveBoost = k_refParticleBoost[i],
        });
    }

    /* THE SOLID CONTROL. Same sprite, same size, same position row — the only
       differences are the two fields that define "does not emit". If this one
       ever blooms, the blend law is not being applied per particle. */
    SpawnParticle((ParticleConfig){
        .position = {s_refParticlePos.x,
                     s_refParticlePos.y - 0.45f * s_refParticleScale,
                     s_refParticlePos.z},
        .velocity = {0.0f, 0.0f, 0.0f},
        .radius = 0.22f * s_refParticleScale,
        .lifetime = 6.0f,
        .colorStart = WHITE,
        .colorEnd = WHITE,
        .render.blendMode = VFX_BLEND_ALPHA,
        .render.unlit = 0,
    });
}
