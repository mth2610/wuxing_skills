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

/* FOUR boosts, not six, and widely spaced.
 *
 * The first version packed six columns at a 0.5 pitch and then grew to four
 * rows. Each glow halo is 4.2x its core, so at that spacing every halo
 * overlapped its neighbours in both axes and the chart became one bright smear —
 * unreadable as an instrument, which is the only thing it is for. A calibration
 * target that cannot be read is worse than none, because it invites judging by
 * eye the very thing it exists to measure.
 *
 * Spacing is now set by the HALO, not the core: pitch > 2x the halo radius, in
 * both axes. */
#define REF_PARTICLE_COUNT 4

static const float k_refParticleBoost[REF_PARTICLE_COUNT] = { 0.5f, 2.0f, 6.0f, 12.0f };

#define REF_PARTICLE_CORE_R 0.16f     /* halo is 4.2x this = 0.672 */
#define REF_PARTICLE_PITCH  1.70f     /* > 2 x halo radius, so nothing overlaps */
#define REF_PARTICLE_ROW    1.55f

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
    TraceLog(LOG_INFO, "VFX_REF_PARTICLE: %d boosts (%.1f .. %.1f) x 3 rows — "
                       "value / glow recipe / negative contrast",
             REF_PARTICLE_COUNT, k_refParticleBoost[0],
             k_refParticleBoost[REF_PARTICLE_COUNT - 1]);
    return 0;
}

void VFX_KillRefParticles(int id) { (void)id; s_refParticlesOn = false; }

/* Respawned on a slow cadence rather than every frame: one spawn per particle
   per frame would stack thousands of coincident billboards and the additive sum,
   not the sprite, would be what got measured. The lifetime outlasts the
   interval, so exactly one generation is ever alive. */
void VC_RefParticles_Update(float dt)
{
    if (!s_refParticlesOn) return;
    s_refParticleTimer -= dt;
    if (s_refParticleTimer > 0.0f) return;
    s_refParticleTimer = 4.0f;

    const float S = s_refParticleScale;
    const float pitch = REF_PARTICLE_PITCH * S;
    const float R = REF_PARTICLE_CORE_R * S;

    for (int i = 0; i < REF_PARTICLE_COUNT; i++)
    {
        const float off = ((float)i - (float)(REF_PARTICLE_COUNT - 1) * 0.5f) * pitch;
        const float B = k_refParticleBoost[i];
        Vector3 at = {s_refParticlePos.x + off, 0.0f, s_refParticlePos.z};

        /* ROW 1 — VALUE CALIBRATION. Achromatic, additive, NO halo: one particle
           whose peak must equal colour x boost. Grey on purpose, so "the value
           changed" and "the hue changed" stay separable. */
        at.y = s_refParticlePos.y + REF_PARTICLE_ROW * S;
        SpawnParticle((ParticleConfig){
            .position = at, .velocity = {0}, .radius = R, .lifetime = 6.0f,
            .colorStart = WHITE, .colorEnd = WHITE,
            .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1,
            .render.emissiveBoost = B,
        });

        /* ROW 2 — THE SHIPPED GLOW RECIPE, saturated, core + halo. */
        at.y = s_refParticlePos.y;
        ParticleSystem_SpawnGlow((ParticleConfig){
            .position = at, .velocity = {0}, .radius = R, .lifetime = 6.0f,
            .colorStart = (Color){70, 110, 255, 255},
            .colorEnd = (Color){70, 110, 255, 255},
            .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1,
            .render.emissiveBoost = B,
        });

        /* ROW 3 — ONE PREMULTIPLIED PARTICLE, which is what FLAME VOLUME's
           shipping population actually uses, and the reason that effect reads on
           bright scenery while the additive rows above do not.
           §5.2's law does both jobs in ONE draw: where coverage is high the
           equation is src (it COVERS the background), where coverage falls to
           zero it is src + dst (it ADDS light). No halo companion, no dark core,
           no second or third particle. */
        at.y = s_refParticlePos.y - REF_PARTICLE_ROW * S;
        SpawnParticle((ParticleConfig){
            .position = at, .velocity = {0}, .radius = R, .lifetime = 6.0f,
            .colorStart = (Color){70, 110, 255, 255},
            .colorEnd = (Color){70, 110, 255, 255},
            .render.blendMode = VFX_BLEND_PREMULTIPLIED,
            .render.unlit = 1,
            .render.emissiveBoost = B,
        });

        /* ROW 4 — NEGATIVE CONTRAST: emissive rim behind, opaque dark core on
           top. The structure that stays legible when the background is brighter
           than anything the effect can emit (§7.6c). */
        at.y = s_refParticlePos.y - 2.0f * REF_PARTICLE_ROW * S;
        SpawnParticle((ParticleConfig){
            .position = at, .velocity = {0}, .radius = R * 3.0f, .lifetime = 6.0f,
            .colorStart = (Color){120, 170, 255, 90},
            .colorEnd = (Color){120, 170, 255, 90},
            .render.texture = ParticleSystem_GlowSprite(),
            .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1,
            .render.emissiveBoost = B * 0.30f,
        });
        SpawnParticle((ParticleConfig){
            .position = at, .velocity = {0}, .radius = R, .lifetime = 6.0f,
            .colorStart = (Color){16, 10, 34, 235},
            .colorEnd = (Color){16, 10, 34, 235},
            .render.blendMode = VFX_BLEND_PREMULTIPLIED,
            .render.unlit = 1,
        });
    }

    /* THE SOLID CONTROL, off to one side. Same sprite and size; the only
       differences are the two fields that define "does not emit". If this one
       ever blooms, the blend law is not being applied per particle. */
    SpawnParticle((ParticleConfig){
        .position = {s_refParticlePos.x - (REF_PARTICLE_COUNT * 0.5f + 0.9f) * pitch,
                     s_refParticlePos.y, s_refParticlePos.z},
        .velocity = {0}, .radius = R, .lifetime = 6.0f,
        .colorStart = WHITE, .colorEnd = WHITE,
        .render.blendMode = VFX_BLEND_ALPHA,
        .render.unlit = 0,
    });
}
