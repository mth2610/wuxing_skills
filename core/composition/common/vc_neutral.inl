// Generic neutral VFX — effects with no elemental identity.
// VFX_ComposeLightningBolt moved to vc_archetype.inl 2026-07-10 — it needs a
// managed pool (ProcBolt requires per-frame Update/Draw with the same id,
// same as VFX_SpawnProcBeam's ProcRay) instead of a stateless single call.
//
// SmokePuff, SmokeTrail: generic smoke/impact residue. Briefly ported to
// VFX_ComposeEnergySmoke's billboard-quad diffusion shader (2026-07-10), but
// reverted the same day — a per-pixel shader (FBM domain-warp + raymarched-
// style density) is far more expensive than an alpha-blended particle sprite,
// and SmokePuff/Trail can fire many times a second from gameplay (path
// pillars, impacts) where EnergySmoke's other caller (ENERGY SMOKE/shockwave
// ring) is a single deliberate cast. Tuned dimmer/smaller/lower-alpha than
// the original particle version to fix the "one glaring bright sphere"
// look — main.c draws ALL particles with BLEND_ADDITIVE (main.c:514), so a
// tight cluster of high-alpha, bright-RGB quads stacks into a blown-out
// white blob; lower alpha + smaller radius + fewer particles keeps the
// additive stacking soft instead of blown out.

// ParticleSystem_SpawnRadialBurst pins every particle's spawn position to
// the exact same origin point (particle_system.c: pcfg.position = origin)
// — they only separate later via velocity. With slow speed + small radius
// that reads as one smooth overlapping circle at birth, not a puff of
// wisps (confirmed by screenshot: "smoke puff nó tròn lẳng vầy nè" — one
// perfectly round blob, not smoke-shaped). Spawning through SpawnParticle
// directly lets each wisp start pre-scattered inside a small sphere, so
// the cluster already reads as irregular from frame 0.
static SkillCurve s_smokeWispBillow = {0}; // grows while fading — see vc_earth.inl's s_earthDustBillow for the same trick
static bool s_smokeWispInit = false;

static void SpawnSmokeWisps(Vector3 pos, float size, int count, float speedMin, float speedMax,
                            float lifeMin, float lifeMax, Color colorStart, Color colorEnd,
                            const ForceField *ff)
{
    if (!s_smokeWispInit) {
        FloatCurve_AddStop(&s_smokeWispBillow, 0.0f, 0.55f);
        FloatCurve_AddStop(&s_smokeWispBillow, 1.0f, 1.6f);
        s_smokeWispInit = true;
    }

    for (int i = 0; i < count; i++) {
        Vector3 scatter = {
            (Random01() - 0.5f) * size * 0.6f,
            (Random01() - 0.5f) * size * 0.35f,
            (Random01() - 0.5f) * size * 0.6f,
        };

        float angle    = Random01() * PI * 2.0f;
        float pitch    = (Random01() - 0.5f) * PI * 0.5f;
        float speed    = Math_Mix(speedMin, speedMax, Random01());
        float cosPitch = cosf(pitch);

        ParticleConfig pcfg = {0};
        pcfg.position = Vector3Add(pos, scatter);
        pcfg.velocity = (Vector3){
            cosf(angle) * speed * cosPitch,
            sinf(pitch) * speed + 0.35f,
            sinf(angle) * speed * cosPitch};
        pcfg.radius     = Math_Mix(size * 0.10f, size * 0.22f, Random01());
        pcfg.lifetime   = Math_Mix(lifeMin, lifeMax, Random01());
        pcfg.colorStart = colorStart;
        pcfg.colorEnd   = colorEnd;
        pcfg.forceField = ff;
        pcfg.radiusCurve = &s_smokeWispBillow;

        SpawnParticle(pcfg);
    }
}

void VFX_ComposeSmokePuff(Vector3 pos, float size)
{
    // size in meters; ~0.3-1.0m = torso-width smoke reference.
    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceField_AddLayer(&f, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 4.5f});
        ForceField_AddLayer(&f, (ForceLayer){
            .type = FORCE_GRAVITY_DIR, .direction = {0.0f, 1.0f, 0.0f}, .strength = 0.6f});
    }

    int count = GetRandomValue(12, 18);
    SpawnSmokeWisps(pos, size, count, 0.2f, 0.45f, 0.7f, 1.2f,
                    (Color){95, 90, 85, 110}, (Color){40, 40, 40, 0}, &f);

    VFXLight_Spawn(pos, (Color){130, 120, 110, 255}, size * 0.8f, 0.12f, VFX_PRIORITY_LOW);
}

void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration)
{
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f) return;
    dir = Vector3Scale(dir, 1.0f / len);

    int numPuffs = (int)(len / 0.5f) + 1;

    Vector3 perp = {-dir.z, 0.0f, dir.x};
    if (Vector3Length(perp) < 0.001f) perp = (Vector3){0.0f, 0.0f, 1.0f};
    perp = Vector3Normalize(perp);

    static ForceField f = {0};
    if (f.layerCount == 0)
        ForceField_AddLayer(&f, (ForceLayer){.type = FORCE_VISCOSITY, .strength = 3.5f});

    for (int i = 0; i <= numPuffs; i++) {
        float t   = (float)i / (float)numPuffs;
        Vector3 p = Vector3Add(start, Vector3Scale(dir, t * len));

        float jitter = ((float)rand() / (float)RAND_MAX * 2.0f - 1.0f) * 0.1f;
        p = Vector3Add(p, Vector3Scale(perp, jitter));

        int count = GetRandomValue(2, 4);
        SpawnSmokeWisps(p, 0.5f, count, 0.1f, 0.2f, duration * 0.7f, duration * 1.3f,
                        (Color){95, 95, 95, 90}, (Color){40, 40, 40, 0}, &f);
    }
}

