// ── F4. Character aura — break the shell ─────────────────────────────────────
//
// WHAT THIS DELIBERATELY DOES NOT USE, and why that is the whole point.
//
// The obvious way to build this is `VFX_ComposeCylinderAura` (aura_shell.fs) for
// the body plus `VFX_ComposeGroundAura` for the feet. That was tried first and
// it is wrong: ELDEN_VFX_SPEC.md §0.1b names `aura_shell.fs` as "the textbook
// plastic bubble look" and lists `ground_aura.fs` among the shaders diagnosed as
// the PROBLEM. Both are one surface with FBM inside it — an architecture with a
// hard ceiling (§0.1b cause 4: one surface = no parallax, no depth). Assembling
// an aura out of the two effects the spec is trying to replace produces a
// rearrangement, not a rebuild, and Đợt E is a rebuild.
//
// So every layer here is built on the F1/F2 foundation instead — the parts that
// have actually been rebuilt: lit particles, authored silhouettes, the blend law.
//
// The three layers (spec §F4), in order of how much they matter:
//
//   1. DISCRETE ELEMENTS crossing the silhouette — the layer that actually reads
//      as an aura. Additive embers orbiting at a radius WIDER than the body, so
//      they pass in front of and behind it. Per the blend law (§F1b) these EMIT,
//      so they are additive and unlit.
//   2. BODY FLOW — soft wisps rising ALONG THE BODY AXIS. This replaces the
//      shell. Alpha-blended and LIT (they occlude), dark-tinted so the F1
//      lighting pass supplies the brightness, with per-sprite spin, a wide size
//      spread and grow/fade curves — i.e. the F2 smoke architecture, aimed
//      upward and wrapped around a body instead of billowing from a point.
//      Directional flow beats uniform scroll: the spec's (a).
//   3. LIGHT EMISSION — a VFXLight tracking the agent, so with E2 the character
//      is lit BY their own aura. Without it an aura always looks pasted on,
//      which is why F4 depends on E2.
//
// Ground contact is the same wisp architecture, flattened and slowed at the
// feet — NOT the old additive disc, which drew at a hard-coded 0.85 opacity and
// blew out into a white starburst that owned the frame.
//
// Sprites are reused from F2 (`s_smokePuffTex`, authored lobed silhouettes):
// common.inl is included before this file in visual_composer.c, so they are in
// scope. A radial-gradient blob has no OUTLINE, and lighting a featureless blob
// yields a lit featureless blob (§0.1b cause 3). E4 should eventually supply
// aura-specific wisp sheets; until then the smoke silhouettes are far closer to
// right than the stock particle dot.
//
// Managed archetype: owns a private pool, driven by VC_CharacterAura_Update /
// _Draw3D from visual_composer.c. Per §0.3 a handle-returning composition needs
// exactly that — a manifest `type` of `continuous` is NOT sufficient.

#define VFX_AURA_MAX 8

// Character metrics, meter scale (root CLAUDE.md): a ~1.8 m humanoid. The ember
// orbit is deliberately WIDER than the torso — motes that stay inside the
// silhouette do not break it, and breaking it is the entire job of layer 1.
#define VFX_AURA_BODY_HEIGHT   1.80f
#define VFX_AURA_BODY_RADIUS   0.30f
#define VFX_AURA_ORBIT_RADIUS  0.52f

typedef struct {
    bool    active;
    int     agentId;
    VC_MaterialId matId;
    float   intensity;      // target, 0..1
    float   smoothed;       // what is actually drawn — ramps toward `intensity`
    float   elapsed;
    float   seed;           // per-slot phase offset so two auras never pulse in lockstep
    float   emberAccum;     // fractional spawn budgets, carried between frames
    float   wispAccum;
    float   groundAccum;
} VC_CharacterAura;

static VC_CharacterAura s_auras[VFX_AURA_MAX];
static int              s_auraNextSerial = 0;

// Shared authored state, built once on first attach.
static SkillCurve    s_auraWispGrow  = {0};
static SkillCurve    s_auraWispFade  = {0};
static SkillCurve    s_auraEmberFade = {0};
static ForceField    s_auraRiseFld   = {0};
static bool          s_auraInit      = false;

// Live-tunable (tuning.cfg, no rebuild). An aura is judged by eye and every one
// of these is a look decision, not a constant — and the first build of this got
// the balance wrong in BOTH directions (old shaders: a white starburst that owned
// the frame; first particle build: so faint it barely read). Dialling that by
// rebuild is exactly the loop core/CLAUDE.md §5 says not to run.
static float s_auraEmberSize  = 1.0f;   // x multiplier on ember radius
static float s_auraEmberRate  = 1.0f;   // x multiplier on embers/sec
static float s_auraWispAlpha  = 1.0f;   // x multiplier on wisp opacity
static float s_auraWispRate   = 1.0f;   // x multiplier on wisps/sec

static void CharacterAura_InitShared(void)
{
    if (s_auraInit) return;

    // Wisps grow modestly — an aura hugs the body, so unlike a dispersing smoke
    // puff (2.2x) it must not balloon away from the silhouette it is tracing.
    FloatCurve_AddStop(&s_auraWispGrow, 0.0f, 0.55f);
    FloatCurve_AddStop(&s_auraWispGrow, 0.30f, 1.0f);
    FloatCurve_AddStop(&s_auraWispGrow, 1.0f, 1.45f);

    // Fade in AND out. A puff can pop in because it is an impact; a persistent
    // aura cannot — any hard edge at spawn reads as flicker once dozens of
    // wisps are cycling continuously.
    FloatCurve_AddStop(&s_auraWispFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_auraWispFade, 0.25f, 1.0f);
    FloatCurve_AddStop(&s_auraWispFade, 0.60f, 0.8f);
    FloatCurve_AddStop(&s_auraWispFade, 1.0f, 0.0f);

    // Embers: bright almost immediately, then a long tail — a spark's afterglow.
    FloatCurve_AddStop(&s_auraEmberFade, 0.0f, 0.0f);
    FloatCurve_AddStop(&s_auraEmberFade, 0.08f, 1.0f);
    FloatCurve_AddStop(&s_auraEmberFade, 1.0f, 0.0f);

    // Buoyancy + drag. Meter scale: real gravity is 9.81, so 0.9 up is a rise
    // you can read without the wisps escaping the body.
    ForceField_AddLayer(&s_auraRiseFld, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = {0.0f, 1.0f, 0.0f},
        .strength = 0.9f,
    });
    ForceField_AddLayer(&s_auraRiseFld, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 2.0f,
    });

    // Registered lazily on first attach, never from an Init: Tuning_RegisterFloat
    // only reads the config once Tuning_Init has set the path (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("aura_ember_size", &s_auraEmberSize, 1.0f);
    Tuning_RegisterFloat("aura_ember_rate", &s_auraEmberRate, 1.0f);
    Tuning_RegisterFloat("aura_wisp_alpha", &s_auraWispAlpha, 1.0f);
    Tuning_RegisterFloat("aura_wisp_rate",  &s_auraWispRate,  1.0f);

    s_auraInit = true;
}

int VFX_ComposeCharacterAura(int agentId, VC_MaterialId matId, float intensity)
{
    CharacterAura_InitShared();
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    // Re-attaching to an agent that already has an aura RETUNES it rather than
    // stacking a second one. Two auras on one body double every layer's density
    // and read as a bug, and the caller has no way to know one already exists.
    // Same convention as StatusVFX_Attach.
    for (int i = 0; i < VFX_AURA_MAX; i++) {
        if (s_auras[i].active && s_auras[i].agentId == agentId) {
            s_auras[i].matId     = matId;
            s_auras[i].intensity = intensity;
            return i;
        }
    }

    int slot = -1;
    for (int i = 0; i < VFX_AURA_MAX; i++) {
        if (!s_auras[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        // Pool full: recycle round-robin so the oldest attachment loses rather
        // than the newest silently failing to appear. Announced, per "make
        // silent paths announce themselves" — an aura that never shows up and
        // an aura that was never requested look identical on screen.
        slot = s_auraNextSerial % VFX_AURA_MAX;
        TraceLog(LOG_WARNING,
                 "VFX_AURA: pool full (%d) — recycling slot %d (agent %d) for agent %d",
                 VFX_AURA_MAX, slot, s_auras[slot].agentId, agentId);
    }
    s_auraNextSerial++;

    s_auras[slot].active      = true;
    s_auras[slot].agentId     = agentId;
    s_auras[slot].matId       = matId;
    s_auras[slot].intensity   = intensity;
    s_auras[slot].smoothed    = 0.0f;   // always ramp UP from nothing — see Update
    s_auras[slot].elapsed     = 0.0f;
    s_auras[slot].seed        = (float)slot * 2.399963f;  // golden-angle-ish spread
    s_auras[slot].emberAccum  = 0.0f;
    s_auras[slot].wispAccum   = 0.0f;
    s_auras[slot].groundAccum = 0.0f;
    return slot;
}

void VFX_AuraSetIntensity(int handle, float intensity01)
{
    if (handle < 0 || handle >= VFX_AURA_MAX || !s_auras[handle].active) return;
    if (intensity01 < 0.0f) intensity01 = 0.0f;
    if (intensity01 > 1.0f) intensity01 = 1.0f;
    // Only the TARGET moves. Update ramps `smoothed` toward it, which is what
    // makes a charge-up read as a charge-up instead of a pop.
    s_auras[handle].intensity = intensity01;
}

void VFX_KillCharacterAura(int handle)
{
    if (handle < 0 || handle >= VFX_AURA_MAX) return;
    s_auras[handle].active = false;
}

static void VC_CharacterAura_Update(float dt)
{
    for (int i = 0; i < VFX_AURA_MAX; i++) {
        VC_CharacterAura *a = &s_auras[i];
        if (!a->active) continue;

        // THE LEAK GUARD. An aura is long-lived and per-agent, so unlike every
        // fire-and-forget composition it has no natural end — if the agent dies
        // or despawns and nobody calls VFX_KillCharacterAura, the slot is held
        // forever and the pool starves. SkillManager_GetAgentPos failing IS the
        // despawn signal, so self-terminate on it rather than trusting callers.
        Vector3 pos;
        if (!SkillManager_GetAgentPos(a->agentId, &pos)) {
            // Announce it: "the agent despawned, as designed" and "you attached
            // to an id that never resolved" are the same silence otherwise, and
            // the second is a caller bug that would present as an aura that
            // simply never appears.
            TraceLog(LOG_WARNING,
                     "VFX_AURA: slot %d released — agent %d no longer resolves "
                     "(despawned, or the id was never valid)", i, a->agentId);
            a->active = false;
            continue;
        }
        a->elapsed += dt;

        // Ramp toward the target — exponential smoothing, framerate-independent.
        // The DoD asks for no popping; assigning intensity straight through is
        // exactly what pops.
        float k = 1.0f - expf(-dt * 4.0f);
        a->smoothed += (a->intensity - a->smoothed) * k;
        if (a->smoothed < 0.002f && a->intensity <= 0.0f) {
            a->active = false;   // faded out on request — free the slot
            continue;
        }

        const VFX_ElementMaterial *mat = VFX_Material(a->matId);
        float t   = a->elapsed;
        float lvl = a->smoothed;

        // ── LAYER 2: BODY FLOW — lit alpha wisps rising along the body axis ──
        // This is what replaces the shell. Emitted around a vertical band at the
        // body radius so the column traces the figure, then carried UP by the
        // force field: directional flow, not a uniform scroll on a bubble.
        a->wispAccum += dt * (14.0f + 26.0f * lvl) * s_auraWispRate;
        int wisps = (int)a->wispAccum;
        if (wisps > 7) wisps = 7;               // clamp after a hitch
        a->wispAccum -= (float)wisps;

        for (int w = 0; w < wisps; w++) {
            float ang = Random01() * 2.0f * PI;
            // Hug the body: a narrow radial band, not a disc, so the wisps read
            // as a sheath around the figure rather than a cloud it stands in.
            float rad = VFX_AURA_BODY_RADIUS * Math_Mix(0.75f, 1.15f, Random01());
            float h   = Math_Mix(0.05f, 0.95f, Random01()) * VFX_AURA_BODY_HEIGHT;
            Vector3 p = { pos.x + cosf(ang) * rad, pos.y + h, pos.z + sinf(ang) * rad };

            // Rise, with a slow swirl around the axis and a slight inward pull
            // toward the body — outward-drifting wisps stop tracing the figure.
            Vector3 tang = VC_TangentXZ(ang, 0.0f);
            Vector3 vel = {
                tang.x * 0.30f - cosf(ang) * 0.10f,
                Math_Mix(0.35f, 0.85f, Random01()),
                tang.z * 0.30f - sinf(ang) * 0.10f,
            };

            // DARK base, element-tinted. Brightness is the lighting pass's job
            // (F1) — authoring bright wisps defeats it, the same reason F2's
            // smoke gradient looks "too dark" in the file.
            Color c = {
                (unsigned char)(mat->body.r / 3 + 22),
                (unsigned char)(mat->body.g / 3 + 22),
                (unsigned char)(mat->body.b / 3 + 22),
                255
            };

            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = vel,
                // Wide, non-uniform radii — uniform sizes read as repetition
                // however good the silhouette is.
                .radius = Math_Mix(0.070f, 0.26f, powf(Random01(), 1.7f)) *
                          (0.7f + 0.5f * lvl),
                .lifetime = Math_Mix(0.6f, 1.2f, Random01()),
                .colorStart = VC_WithAlpha(c, (unsigned char)(115.0f * lvl * s_auraWispAlpha)),
                .colorEnd = VC_WithAlpha(c, 0),
                .forceField = &s_auraRiseFld,
                .radiusCurve = &s_auraWispGrow,
                .alphaCurve = &s_auraWispFade,
                .render.texture = s_smokePuffTex[w % SMOKE_PUFF_VARIANTS],
                .render.blendMode = VFX_BLEND_ALPHA,   // occludes -> alpha, and lit
                // Per-sprite spin: without it the repeated texture is obvious
                // no matter how many sprites are stacked.
                .rotation = Random01() * 2.0f * PI,
                .angularVelocity = (Random01() - 0.5f) * 1.1f,
            });
        }

        // ── LAYER 1: DISCRETE EMBERS crossing the silhouette (most important) ─
        // Orbit radius > body radius, so these pass in front of and behind the
        // figure. They EMIT, so per the blend law they are additive and unlit —
        // running them through the lighting multiply would brown them out.
        a->emberAccum += dt * (18.0f + 40.0f * lvl) * s_auraEmberRate;
        int embers = (int)a->emberAccum;
        if (embers > 9) embers = 9;
        a->emberAccum -= (float)embers;

        for (int m = 0; m < embers; m++) {
            float phase = a->seed + (float)m * 1.7f + Random01() * 0.6f;
            float ang   = t * 2.0f + phase;
            // Slight radius jitter so the band is not a wire-perfect circle.
            float rad = VFX_AURA_ORBIT_RADIUS * Math_Mix(0.85f, 1.25f, Random01());
            Vector3 p = {
                pos.x + cosf(ang) * rad,
                pos.y + Math_Mix(0.05f, 1.0f, Random01()) * VFX_AURA_BODY_HEIGHT,
                pos.z + sinf(ang) * rad,
            };
            // Tangential velocity keeps each ember on its arc after spawn, so
            // the orbit survives the particle system's own ballistic update.
            float rise = Math_Mix(0.5f, 1.1f, VC_Flicker01(t, phase));
            Vector3 vel = Vector3Scale(VC_TangentXZ(ang, rise / 0.6f), 0.6f);

            // Hot identity colour — glow, not body: an ember is the emissive
            // highlight of the element.
            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = vel,
                .radius   = Math_Mix(0.026f, 0.058f, Random01()) * (0.7f + 0.5f * lvl) * s_auraEmberSize,
                .lifetime = Math_Mix(0.45f, 0.95f, Random01()),
                .colorStart = VC_WithAlpha(mat->glow, (unsigned char)(235.0f * lvl)),
                .colorEnd = VC_WithAlpha(mat->soft, 0),
                .alphaCurve = &s_auraEmberFade,
                .render.blendMode = VFX_BLEND_ADDITIVE, // emits -> additive...
                .render.unlit = 1,                      // ...and stays unlit
            });
        }

        // ── GROUND CONTACT — the same wisps, flattened and slowed ────────────
        // An aura floating free of the floor reads as a decal. This is NOT the
        // old additive ground disc: these are wide, slow, LIT alpha wisps that
        // creep outward at ankle height, so the contact is a soft skirt of
        // element-tinted haze rather than a glowing plate under the feet.
        a->groundAccum += dt * (4.0f + 8.0f * lvl);
        int gwisps = (int)a->groundAccum;
        if (gwisps > 3) gwisps = 3;
        a->groundAccum -= (float)gwisps;

        for (int g = 0; g < gwisps; g++) {
            float ang = Random01() * 2.0f * PI;
            float rad = Math_Mix(0.10f, 0.42f, sqrtf(Random01()));
            Vector3 p = { pos.x + cosf(ang) * rad, pos.y + 0.04f, pos.z + sinf(ang) * rad };
            Vector3 vel = { cosf(ang) * 0.22f, 0.05f, sinf(ang) * 0.22f };

            Color c = {
                (unsigned char)(mat->body.r / 3 + 18),
                (unsigned char)(mat->body.g / 3 + 18),
                (unsigned char)(mat->body.b / 3 + 18),
                255
            };
            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = vel,
                .radius   = Math_Mix(0.10f, 0.26f, Random01()) * (0.8f + 0.4f * lvl),
                .lifetime = Math_Mix(0.9f, 1.6f, Random01()),
                .colorStart = VC_WithAlpha(c, (unsigned char)(52.0f * lvl)),
                .colorEnd = VC_WithAlpha(c, 0),
                .radiusCurve = &s_auraWispGrow,
                .alphaCurve = &s_auraWispFade,
                .render.texture = s_smokePuffTex[g % SMOKE_PUFF_VARIANTS],
                .render.blendMode = VFX_BLEND_ALPHA,
                .rotation = Random01() * 2.0f * PI,
                .angularVelocity = (Random01() - 0.5f) * 0.5f,
            });
        }

        // ── LAYER 3: real light emission, tracking the agent ─────────────────
        // Re-spawned every frame with a lifetime just over one frame: the pool's
        // own expiry stops it accumulating and it follows the agent for free
        // (same trick as VFXLight_DebugTestLight). LOW priority — a persistent
        // aura must never evict a one-shot ultimate's light.
        VFXLight_Spawn((Vector3){pos.x, pos.y + VFX_AURA_BODY_HEIGHT * 0.5f, pos.z},
                       VC_WithAlpha(mat->soft, (unsigned char)(200.0f * lvl)),
                       2.2f + 1.2f * lvl, 0.05f, VFX_PRIORITY_LOW);
    }
}

// Everything this archetype draws is spawned into the particle system, which
// owns its own lit/blended draw pass — so there is nothing to render here. The
// pair is kept because visual_composer.c dispatches Update/Draw together and a
// silently missing half is the exact wiring bug §0.3 warns about.
static void VC_CharacterAura_Draw3D(Camera3D cam)
{
    (void)cam;
}
