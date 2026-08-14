// Cosmic black hole / gravity singularity — "sphere + polar-swirl shader"
// pipeline (cheap-to-expensive ranking: raymarch > shader swirl > concentric
// shells > ribbons > GPU particles > vector field > volume texture — this
// uses the shader-swirl + concentric-shells combo, the right cost/quality
// point for an MMORPG with many simultaneous VFX, no raymarching needed).
// Intended use: `pos.y` well above 0 (floating over the arena) — the whole
// point is a levitating singularity that drains matter UP off the ground
// beneath it, not a ground-level effect.
// Layers: (1) near-black opaque event-horizon sphere via EffectMaterial's
// dark-body + thin fresnel rim technique; (2) SWIRL SHELLS — 3 concentric
// spheres at increasing radii, all sharing `black_hole_swirl.fs`
// (core/shaders/black_hole_swirl.fs): the shader converts the sphere's own
// longitude/latitude UV into a polar (angle, radius) pair, spins+warps the
// angle by radius, samples FBM in that domain, then exponentially fades
// density away from the equator band — a swirling ring texture painted
// directly onto the sphere surface, never a flat plane cutting through it.
// Each shell gets a different radius/noiseScale/counter-rotation direction
// (fake-volume trick: several thin noisy shells read as one turbulent
// gaseous mass, far cheaper than real raymarched volume); (3) close-range
// infalling particles using the exact spawn-on-shell + velocity-toward-
// center technique VFX_SummonCircle already uses for its "pulling particles
// in" step, just spawned on a sphere shell instead of a ground ring;
// (4) GROUND DRAIN — the dramatic long-range pull: particles spawn
// scattered on the actual ground plane under the singularity and fly
// straight up into it, lifetime scaled to travel distance so they visibly
// arrive instead of expiring mid-flight; (5) a rotating ground rune marking
// the drain point (`mat->runeDecal`, same primitive VFX_ComposeShield uses
// for its own ground circle); (6) gravitational-lensing screen
// distortion; (7) a dim ambient light — deliberately subtle, a black hole
// doesn't blaze.
void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time)
{
    if (radius <= 0.0f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    float dt = GetFrameTime();

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 1: EVENT HORIZON — opaque near-black sphere, thin colored rim
    // only (translucency=0 keeps it fully opaque regardless of fresnel;
    // baseColor stays black under diffuse lighting, rim is the only light
    // that escapes — visually "light can't escape except at the edge").
    // ─────────────────────────────────────────────────────────────────────
    static EffectMaterial s_coreMat;
    static bool s_coreMatLoaded = false;
    if (!s_coreMatLoaded)
    {
        EffectMaterialParams p = {0};
        p.baseColor          = (Color){8, 4, 14, 255}; // near-black, faint cool violet tint
        p.rimStrength        = 2.2f;
        p.fresnelPower       = 5.5f; // thin, sharp rim — not a soft glow
        p.emissiveIntensity  = 0.0f;
        p.distortionStrength = 0.0f;
        p.translucency       = 0.0f; // fully opaque body
        Material_LoadCustom(&s_coreMat, &p);
        s_coreMatLoaded = true;
    }
    Material_Begin(s_coreMat);
    DrawCoreSphere(pos, radius * 0.5f, 28, 28, WHITE);
    Material_End();

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 2: SWIRL SHELLS — 3 concentric spheres sharing one shader
    // (black_hole_swirl.fs), each a different radius/noiseScale/spin
    // direction. Fake-volume trick: several thin noisy shells stacked
    // together read as one turbulent gaseous mass without real raymarching.
    // Hard-coded violet instead of mat->body/glow — deliberate identity
    // break: a black hole reads as "cosmic void" regardless of which
    // VC_MaterialId the caller passes (a METAL- or FIRE-flavored singularity
    // should still look like a black hole first, elemental second). Only
    // the particle/rune layers below use mat->body/glow so the caster's
    // element still shows through the debris being pulled in.
    // ─────────────────────────────────────────────────────────────────────
    static Shader s_swirlSh       = {0};
    static int    s_uSwirlBody    = -1;
    static int    s_uSwirlGlow    = -1;
    static int    s_uSwirlOpacity = -1;
    static int    s_uSwirlSpeed   = -1;
    static int    s_uSwirlNoise   = -1;
    static int    s_uSwirlBand    = -1;
    static int    s_uSwirlTime    = -1;
    static bool   s_swirlInit     = false;
    if (!s_swirlInit)
    {
        s_swirlSh       = ResourceManager_LoadShader("core/shaders/ground_aura.vs",
                                                      "core/shaders/black_hole_swirl.fs");
        s_uSwirlBody    = GetShaderLocation(s_swirlSh, "u_bodyColor");
        s_uSwirlGlow    = GetShaderLocation(s_swirlSh, "u_glowColor");
        s_uSwirlOpacity = GetShaderLocation(s_swirlSh, "u_opacity");
        s_uSwirlSpeed   = GetShaderLocation(s_swirlSh, "u_swirlSpeed");
        s_uSwirlNoise   = GetShaderLocation(s_swirlSh, "u_noiseScale");
        s_uSwirlBand    = GetShaderLocation(s_swirlSh, "u_bandWidth");
        s_uSwirlTime    = GetShaderLocation(s_swirlSh, "u_time");
        s_swirlInit     = true;
    }

    VFXRenderScope swirlScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    SkillManager_BeginShader(s_swirlSh);

    float tShader = (float)GetTime();
    if (s_uSwirlTime >= 0) SetShaderValue(s_swirlSh, s_uSwirlTime, &tShader, SHADER_UNIFORM_FLOAT);

    Vector4 bodyV = ColorNormalize((Color){40, 10, 60, 255});   // deep violet, barely visible face-on
    Vector4 glowV = ColorNormalize((Color){210, 170, 255, 255}); // bright violet-white bent-light accent
    if (s_uSwirlBody >= 0) SetShaderValue(s_swirlSh, s_uSwirlBody, &bodyV, SHADER_UNIFORM_VEC4);
    if (s_uSwirlGlow >= 0) SetShaderValue(s_swirlSh, s_uSwirlGlow, &glowV, SHADER_UNIFORM_VEC4);

    rlDisableBackfaceCulling();

    // Shell 0: innermost, tight band, fast counter-spin, right against the core.
    // Shell 1: mid, wider band, slower forward spin.
    // Shell 2: outermost, widest/dimmest band, slow reverse spin — reads as
    // the faint outer haze of the accretion mass.
    struct { float radiusScale, opacity, speed, noiseScale, bandWidth; } shells[3] = {
        {0.56f, 0.85f, -2.2f, 4.5f, 0.28f},
        {0.72f, 0.55f,  1.4f, 3.2f, 0.42f},
        {0.92f, 0.30f, -0.7f, 2.4f, 0.60f},
    };
    for (int i = 0; i < 3; i++)
    {
        float opacity = shells[i].opacity;
        float speed   = shells[i].speed;
        float noise   = shells[i].noiseScale;
        float band    = shells[i].bandWidth;
        if (s_uSwirlOpacity >= 0) SetShaderValue(s_swirlSh, s_uSwirlOpacity, &opacity, SHADER_UNIFORM_FLOAT);
        if (s_uSwirlSpeed   >= 0) SetShaderValue(s_swirlSh, s_uSwirlSpeed,   &speed,   SHADER_UNIFORM_FLOAT);
        if (s_uSwirlNoise   >= 0) SetShaderValue(s_swirlSh, s_uSwirlNoise,   &noise,   SHADER_UNIFORM_FLOAT);
        if (s_uSwirlBand    >= 0) SetShaderValue(s_swirlSh, s_uSwirlBand,    &band,    SHADER_UNIFORM_FLOAT);

        DrawCoreSphere(pos, radius * shells[i].radiusScale, 22, 22, WHITE);
        rlDrawRenderBatchActive(); // flush before next shell's uniforms take effect
    }

    rlEnableBackfaceCulling();
    VFXRender_EndDraw(&swirlScope);
    SkillManager_EndShader();

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 3: CLOSE-RANGE INFALL — spawn on a sphere shell around the
    // singularity, velocity straight toward center. Same technique as
    // VFX_SummonCircle's pull-in step, just spawned on a 3D shell (uniform
    // sphere sampling) instead of a flat ground ring.
    // ─────────────────────────────────────────────────────────────────────
    if (Random01() < (18.0f * dt))
    {
        float theta = Random01() * 2.0f * PI;
        float u     = Random01() * 2.0f - 1.0f; // cos(phi), uniform on sphere
        float s     = sqrtf(fmaxf(0.0f, 1.0f - u * u));
        Vector3 dir = {cosf(theta) * s, u, sinf(theta) * s};

        float dist = radius * (1.4f + Random01() * 0.6f);
        Vector3 spawnPos = Vector3Add(pos, Vector3Scale(dir, dist));
        Vector3 toCenter = Vector3Normalize(Vector3Subtract(pos, spawnPos));
        Vector3 vel      = Vector3Scale(toCenter, 1.8f + Random01() * 0.8f);

        SpawnParticle((ParticleConfig){
            .position   = spawnPos,
            .velocity   = vel,
            .radius     = (0.015f + Random01() * 0.02f) * radius,
            .lifetime   = 0.55f,
            .colorStart = VC_WithAlpha(mat->glow, 220),
            .colorEnd   = VC_WithAlpha(mat->glow, 0),
        });
    }

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 4: GROUND DRAIN — the dramatic long-range pull. Particles spawn
    // scattered on the actual ground plane directly under the singularity
    // and fly straight up into it (velocity aimed at `pos`, not just "up" —
    // reads correctly even if the hole drifts off-center from its own drain
    // point). Lifetime is distance/speed so they visibly arrive at the
    // event horizon instead of expiring mid-flight or overshooting.
    // ─────────────────────────────────────────────────────────────────────
    if (Random01() < (10.0f * dt))
    {
        float theta   = Random01() * 2.0f * PI;
        float groundR = radius * (0.3f + Random01() * 1.8f); // scattered under + a bit past the hole's own footprint
        Vector3 spawnPos = {
            pos.x + cosf(theta) * groundR,
            0.03f, // ground level (project convention: Y=0 is ground)
            pos.z + sinf(theta) * groundR};

        float dist  = Vector3Distance(spawnPos, pos);
        float speed = 3.0f + Random01() * 1.5f;
        Vector3 vel = Vector3Scale(Vector3Normalize(Vector3Subtract(pos, spawnPos)), speed);
        float life  = Clamp(dist / speed, 0.3f, 3.0f);

        SpawnParticle((ParticleConfig){
            .position   = spawnPos,
            .velocity   = vel,
            .radius     = (0.02f + Random01() * 0.025f) * radius,
            .lifetime   = life,
            .colorStart = VC_WithAlpha(mat->body, 210),
            .colorEnd   = VC_WithAlpha(mat->glow, 0),
        });
    }

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 5: GROUND RUNE — marks the drain point directly below the
    // singularity, same primitive VFX_ComposeShield uses for its own ground
    // circle. Slow spin, radius scales with the singularity so a bigger
    // black hole visibly drains a wider patch of ground.
    // ─────────────────────────────────────────────────────────────────────
    {
        const char *runePath = mat->runeDecal;
        if (runePath != NULL)
        {
            Texture2D runeTex = ResourceManager_LoadTexture(runePath);
            VFXRenderScope runeScope = VFXRender_BeginDraw(
                VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
            VC_DrawGroundRune(runeTex, (Vector3){pos.x, 0.02f, pos.z},
                              radius * 1.6f, time * 14.0f, VC_WithAlpha(mat->glow, 160));
            VFXRender_EndDraw(&runeScope);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 6: GRAVITATIONAL LENSING — probabilistic gate (Random01() < rate*dt),
    // same convention as GroundAura's edge sparks, avoids a shared static
    // timer misfiring across multiple simultaneous black-hole instances.
    // ─────────────────────────────────────────────────────────────────────
    if (Random01() < (5.0f * dt))
    {
        ScreenDistort_Add(pos, radius * 2.2f, 0.035f, 0.35f, 1.6f);
    }

    // ─────────────────────────────────────────────────────────────────────
    // LAYER 7: dim ambient light — a black hole doesn't blaze, keep it subtle.
    // ─────────────────────────────────────────────────────────────────────
    if (Random01() < (4.0f * dt))
    {
        VFXLight_Spawn(pos, mat->glow, radius * 1.2f, 0.18f, VFX_PRIORITY_LOW);
    }
}
