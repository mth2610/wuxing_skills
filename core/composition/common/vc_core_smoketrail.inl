// ── VFX_ComposeSmokeTrail — soft ribbon trail that curls upward as it drifts ──
// ── VFX_ComposeEnergyTrail — Sin-Wave Energy Ribbon (GPU deform, packed wisp) ──
//
// Ribbon-trail authoring guide, mapped onto TrailConfig (trail_system.h):
//
//   1. RIBBON TRAIL  -> TRAIL_SHAPE_RIBBON + TRAIL_TYPE_FOLLOWER, camera-facing,
//                        attached to the moving head with Trail_AttachToTransform
//                        so the engine recomputes the tip every frame — no manual
//                        per-frame position feed needed.
//   2. WIDTH/ALPHA   -> TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE: a compact source,
//                        broad mature plume, then a transparent dissipating tail.
//   3. FLOW/NOISE    -> exactly ONE textured layer. A continuous untextured
//                        halo fills every transparent gap and makes smoke read
//                        as an energy beam, so it is deliberately absent here.
//                        uvMetresPerTile scrolls by metres of path travelled,
//                        not by emitter speed, so the flow reads the same
//                        whether the head is fast or slow.
//   4. SHADER FEEL   -> alpha blend, dark soft-edged puffs, no hot core. Smoke
//                        must be allowed to occlude itself; additive is only
//                        appropriate for a separate ember/glow layer.
//   5. CURL UPWARD    -> forceField = FORCE_PRESET_FIRE_UPDRAFT perturbs the
//                        cloth; nodeHomeSpring springs each node back toward
//                        where it was laid so the ribbon curls along the swept
//                        path instead of writhing free of its own trail
//                        (see trail_system.h's comment on nodeHomeSpring).
//
// Use VFX_SmokeTrail_Stop(id) when emission ends: it detaches the head and lets
// the already-laid ribbon billow and dissolve. KillTrail(id) remains available
// for an intentional immediate cut.
//
// Signature matches the prototype already declared in visual_composer.h:
//   int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
//                             float radius, float lifetime);
// Colour comes from VFX_Material(mat) (Composition rule, API.md) rather than a
// texture/tint argument. The body uses smoke_ribbon.png by default; callers
// may replace it with VFX_SmokeTrail_SetTexture() before or after init.

static ForceField s_smokeTrailUpdraft;
static TrailLayer s_smokeTrailLayers[1];
static bool s_smokeTrailInit = false;
static Texture2D s_smokeTrailSheet = {0};
static Texture2D s_smokeTrailFlowMap = {0};
static Texture2D s_smokeTrailMask = {0};

static float s_smokeTrailCurlStrength = 1.0f; // 0 = straight ribbon, 1 = full updraft curl
static float s_smokeTrailScrollSpeed = 0.16f; // tiles/sec — slow reads as smoke, fast reads as water
static float s_smokeTrailFlowSpeed = 0.34f;
static float s_smokeTrailFlowStrength = 0.14f;
static float s_smokeTrailDissolve = 0.28f;
static float s_smokeTrailMaskTiling = 1.35f;

// VFX_ComposeEnergyTrail (GPU-deformed energy smoke) — deform params,
// meter-scale (radius ~0.10-0.25, amplitudes stay a fraction of it).
// The look is "flat energy smoke with torn edges": vertex deform is OFF
// (the waves read as rigid and hid the silhouette), while the packed material
// shader still erodes the edges (u_edgeTear), widens to a full tail
// (SMOKE_WIDEN envelope) and evaporates the tip (tailFade). The sin/curl
// tunables below stay for re-enabling vertex deform via cfg.
static float s_energyTrailAmpSide = 0.090f;    // octave-0 amplitude along the width (sin-multi mode)
static float s_energyTrailAmpNorm = 0.050f;    // octave-0 amplitude along the strip normal — the "swim" axis, keep under side
static float s_energyTrailFreq = 1.20f;        // octave-0 frequency (cycles along the whole path) — LOW = long sweeps
static float s_energyTrailSpeed = 4.50f;       // octave-0 crest travel speed — high = the wave visibly flows
static float s_energyTrailStrength = 0.22f;    // global wave scale (curl3: noise 0..1 -> max ~0.31 m at 0.22)
static float s_energyTrailEnvHead = 0.10f;     // wave dies over the first 10% of seg
static float s_energyTrailEnvTail = 0.88f;     // wave calms over the last 12% of seg
static float s_energyTrailWispMix = 0.35f;     // coarse(R) -> fine(G)
static float s_energyTrailDissolve = 0.30f;    // B-channel dissolve threshold — low so the band survives
static float s_energyTrailTurb = 0.50f;        // A-channel mix jitter
static float s_energyTrailEdgeTear = 0.35f;    // dissolve-threshold noise at the edges — torn silhouette
static float s_energyTrailTilingX = 3.0f;      // wisp tiles along the path
static float s_energyTrailPanCoarse = 1.20f;   // coarse wisp pan (UV units/sec) — clearly visible flow
static float s_energyTrailPanFine = 2.00f;     // fine wisp pan — must differ from coarse for internal motion

// The packed 4-channel wisp sheet (R coarse, G fine, B dissolve, A turbulence)
// lives in the surface registry's slot for the energy trail; the layer owns a
// pointer so trails stay engine-owned (no per-trail texture handles).
static Texture2D s_energyWispSheet = {0};
static TrailLayer s_energyTrailLayer[1] = {0};

// Caller may override the built-in sheet, but the default is never NULL: smoke
// that has no alpha sheet can only render as a flat coloured ribbon.
static const Texture2D *s_smokeTrailTexture = NULL;

static float SmokeTrail_Clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;
    return (value > 1.0f) ? 1.0f : value;
}

// Tuning.cfg is hot-reloaded while trails are alive.  SpawnTrailEntity copies
// config values into TrailEntity, so smoke must explicitly copy the live knobs
// back every frame; otherwise edits only affect the next spawned trail.
static void SmokeTrail_ApplyLiveTuning(TrailEntity *trail)
{
    float curl = SmokeTrail_Clamp01(s_smokeTrailCurlStrength);
    trail->uvScrollSpeed = s_smokeTrailScrollSpeed;
    trail->flowSpeed = s_smokeTrailFlowSpeed;
    trail->flowStrength = (s_smokeTrailFlowStrength > 0.0f)
                              ? s_smokeTrailFlowStrength : 0.0f;
    trail->useFlowMap = (trail->flowMap.id != 0 && trail->flowStrength > 0.0f);
    trail->dissolve = SmokeTrail_Clamp01(s_smokeTrailDissolve);
    trail->maskTiling = (s_smokeTrailMaskTiling > 0.0f)
                            ? s_smokeTrailMaskTiling : 1.0f;

    if (curl <= 0.0f)
    {
        trail->forceField = NULL;
        trail->nodeHomeSpring = 0.0f;
        trail->nodeHomeMaxDev = 0.0f;
        trail->nodeOrderFrac = 0.0f;
        return;
    }

    trail->forceField = &s_smokeTrailUpdraft;
    trail->nodeHomeSpring = Math_Mix(0.35f, 0.70f, curl);
    trail->nodeHomeMaxDev = 0.08f;
    trail->nodeOrderFrac = 0.18f;
}

static void SmokeTrail_OnUpdate(int trailId, float dt)
{
    (void)dt;
    TrailEntity *trail = GetTrail(trailId);
    if (trail != NULL && trail->active)
        SmokeTrail_ApplyLiveTuning(trail);
}

void VFX_SmokeTrail_SetTexture(const Texture2D *smokeTex)
{
    s_smokeTrailTexture = smokeTex;
    if (s_smokeTrailInit)
        s_smokeTrailLayers[0].texture = smokeTex;
}

static void SmokeTrail_InitShared(void)
{
    if (s_smokeTrailInit)
        return;

    s_smokeTrailUpdraft = ForceField_CreatePreset(FORCE_PRESET_FIRE_UPDRAFT);
    const VFX_SurfaceProfile *smokeProfile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_SMOKE_RIBBON);
    s_smokeTrailSheet = smokeProfile != NULL ? smokeProfile->body : (Texture2D){0};
    s_smokeTrailFlowMap = smokeProfile != NULL ? smokeProfile->flowMap : (Texture2D){0};
    s_smokeTrailMask = smokeProfile != NULL ? smokeProfile->mask : (Texture2D){0};
    if (s_smokeTrailSheet.id > 0)
    {
        SetTextureFilter(s_smokeTrailSheet, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_smokeTrailSheet, TEXTURE_WRAP_REPEAT);
        if (s_smokeTrailTexture == NULL)
            s_smokeTrailTexture = &s_smokeTrailSheet;
    }
    else
    {
        TraceLog(LOG_WARNING, "SMOKE TRAIL: smoke_ribbon.png missing — body is untextured");
    }
    if (s_smokeTrailFlowMap.id > 0)
    {
        SetTextureFilter(s_smokeTrailFlowMap, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_smokeTrailFlowMap, TEXTURE_WRAP_REPEAT);
    }
    else
    {
        TraceLog(LOG_WARNING, "SMOKE TRAIL: smoke_ribbon_flow.png missing — no UV distortion");
    }
    if (s_smokeTrailMask.id > 0)
    {
        SetTextureFilter(s_smokeTrailMask, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_smokeTrailMask, TEXTURE_WRAP_REPEAT);
    }
    else
    {
        TraceLog(LOG_WARNING, "SMOKE TRAIL: smoke_ribbon_mask.png missing — no erosion mask");
    }

    // One textured body only. A second untextured halo would bridge every gap
    // between the puffs and turn the ribbon back into a solid energy field.
    s_smokeTrailLayers[0] = (TrailLayer){
        .widthMul = 1.0f,
        .alphaMul = 0.72f,
        .whiten = 0.0f,
        .scrollMul = 1.0f,
        .headAlphaPow = 0.0f,
        .texture = s_smokeTrailTexture,
    };

    Tuning_RegisterFloat("smoketrail_curl_strength", &s_smokeTrailCurlStrength, 1.0f);
    Tuning_RegisterFloat("smoketrail_scroll_speed", &s_smokeTrailScrollSpeed, 0.16f);
    Tuning_RegisterFloat("smoketrail_flow_speed", &s_smokeTrailFlowSpeed, 0.34f);
    Tuning_RegisterFloat("smoketrail_flow_strength", &s_smokeTrailFlowStrength, 0.14f);
    Tuning_RegisterFloat("smoketrail_dissolve", &s_smokeTrailDissolve, 0.28f);
    Tuning_RegisterFloat("smoketrail_mask_tiling", &s_smokeTrailMaskTiling, 1.35f);
    s_smokeTrailInit = true;
}

// followTransform : the moving head this trail chases. Caller owns the Matrix
//                    and must keep it valid for the trail's lifetime (it is
//                    re-sampled every frame by the engine, not just once here).
// mat             : element material — colour comes from VFX_Material(mat),
//                    never ad-hoc RGB (Composition rule, API.md).
// radius          : ribbon thickness at the head, IN METRES.
//                    *** KEEP THIS SMALL: ~0.10-0.20 (API.md meter-scale rule).
//                    A radius anywhere near 1.0+ is bigger than the whole
//                    ribbon's early length (the first few history nodes sit
//                    only centimetres apart right after spawn), so the cross-
//                    section swamps the swept path — the shape reads as a fat
//                    blob whose WIDTH appears to curl, instead of a thin
//                    ribbon whose LENGTH curls, and the near-degenerate quads
//                    at the head render as a dark wedge. If your trail looks
//                    like a lopsided comet with a black triangle at the base,
//                    this is almost always an oversized radius, not a logic bug. ***
// lifetime        : seconds of trail memory at the authored sample rate.
// returns a trail handle. Call VFX_SmokeTrail_Stop(handle) to release it.
int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float radius, float lifetime)
{
    SmokeTrail_InitShared();
    if (radius <= 0.0f || radius > 0.6f)
        radius = 0.15f; // clamp obviously-wrong scale rather than render a degenerate ribbon
    if (lifetime <= 0.0f)
        lifetime = 1.2f; // smoke lingers longer than a typical energy trail

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Vector3 headPos = {followTransform->m12, followTransform->m13, followTransform->m14};

    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.shape = TRAIL_SHAPE_RIBBON;
    cfg.pos = headPos; // seeds the first history node before attach kicks in
    cfg.thick = radius;
    cfg.life = 1.0e6f; // emission lives until stopped; the detached tail fades itself
    cfg.tint = VC_WithAlpha(m->body, 190);
    cfg.disableInnerCore = true;                         // smoke tint, not the hot glow tone
    cfg.blendMode = BLEND_ALPHA;
    // BLEND_ALPHA is enum value 0.  Without this flag the legacy trail
    // fallback treats zero as "unspecified" and silently draws additive.
    cfg.useCustomBlendMode = true;
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE;
    cfg.layers = s_smokeTrailLayers;
    cfg.layerCount = 1;
    cfg.uvMetresPerTile = radius * 10.0f;        // broad puffs, never fine energy filaments
    cfg.uvScrollSpeed = s_smokeTrailScrollSpeed; // slow drift reads as smoke, not water
    cfg.flowMap = &s_smokeTrailFlowMap;
    cfg.useFlowMap = (s_smokeTrailFlowMap.id != 0);
    cfg.flowSpeed = s_smokeTrailFlowSpeed;
    cfg.flowStrength = s_smokeTrailFlowStrength;
    cfg.flowTiling = 1.0f;
    cfg.noiseMask = &s_smokeTrailMask;
    cfg.dissolve = s_smokeTrailDissolve;
    cfg.maskTiling = s_smokeTrailMaskTiling;
    cfg.sampleHz = 30.0f;                        // fixed-rate nodes -> length independent of fps
    cfg.trailLength = lifetime * cfg.sampleHz;   // segment memory is authored in seconds
    cfg.idleSpeed = 0.05f;                       // stop laying nodes once the head is basically still
    cfg.teleportSpeed = 25.0f;                   // a bigger jump cuts+restarts, no straight bridge
    // Catmull-Rom overshoots at a sharp U-turn, causing the strip to fold over
    // itself. Fixed-rate 30 Hz follower nodes are already smooth enough here.
    cfg.smoothSpline = false;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.onUpdate = SmokeTrail_OnUpdate;

    // 5. CURL UPWARD. The updraft perturbs the swept path; nodeHomeSpring keeps
    // the ribbon sprung back toward where it was laid so it curls in place
    // instead of tearing itself loose from its own trail.
    if (SmokeTrail_Clamp01(s_smokeTrailCurlStrength) > 0.0f)
    {
        cfg.forceField = &s_smokeTrailUpdraft;
        cfg.nodeHomeSpring = Math_Mix(0.35f, 0.70f, SmokeTrail_Clamp01(s_smokeTrailCurlStrength));
        cfg.nodeHomeMaxDev = 0.08f; // subtle billow, never enough to fold the ribbon
        cfg.nodeOrderFrac = 0.18f;  // preserve node order through a tight turn
    }

    int id = SpawnTrailEntity(cfg);
    Trail_AttachToTransform(id, followTransform, (Vector3){0, 0, 0});
    return id;
}

void VFX_SmokeTrail_Stop(int trailId)
{
    // Detaching preserves the ribbon history. UpdateFollowerPhysics then drains
    // its already-transparent tail instead of popping it out of existence.
    Trail_AttachToTransform(trailId, NULL, (Vector3){0.0f, 0.0f, 0.0f});
}

// ═══════════════════════════════════════════════════════════════════════════
// VFX_ComposeEnergyTrail — the Sin-Wave Energy Ribbon
// ═══════════════════════════════════════════════════════════════════════════
// The GPU half of the trail pipeline (trail_deform.vs/.fs), mapped onto the
// engine's stages:
//
//   1. POINT SAMPLER (CPU)    -> TRAIL_TYPE_FOLLOWER history ring + sampleHz
//                                (fixed-rate nodes, engine-owned, no malloc)
//   2. RIBBON BUILDER         -> TRAIL_SHAPE_RIBBON; for deform trails the
//                                texcoord.y carries the normalized segment
//                                0=head..1=tail (wave envelope input), and the
//                                strip's side vector rides the normal slot via
//                                DrawRibbonStripDeformedEx.
//   3. VERTEX SIN-WAVES       -> trail_deform.vs, mode SIN_MULTI: 3 octaves
//                                along `side` + 3 along the strip normal, each
//                                with its own frequency/travel speed, stacked
//                                so the motion reads amorphous, not cyclic.
//                                u_wavePhase is randomized per spawn so no two
//                                casts look identical.
//   4. PACKED 4-CHANNEL TEX   -> trail_deform.fs: ONE RGBA texture where
//                                R = coarse wisp, G = fine wisp, B = dissolve
//                                noise, A = turbulence; R+B and G+A pan at
//                                different speeds for internal motion. The
//                                sheet is assets/textures/energy_wisp.png
//                                (registry VFX_SURFACE_ENERGY_RIBBON), authored
//                                multi-channel — smoke_ribbon.png is
//                                single-channel and must never stand in.
//   5. OUTPUT                 -> cfg.blendMode = BLEND_ADDITIVE; the body pass
//                                still gets the colored alpha version and the
//                                emission pass the additive glow, so the hue
//                                survives bright scenes (VFX render-layer
//                                contract in core/docs/API_GUIDE.md).
//
// Shape comes from the shader, NOT from CPU cloth: no forceField, no
// nodeHomeSpring — the swept path stays the spine, the waves live in the
// vertex program. Same stop semantics as the smoke trail: VFX_SmokeTrail_Stop.
int VFX_ComposeEnergyTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float radius, float lifetime)
{
    SmokeTrail_InitShared();
    if (radius <= 0.0f || radius > 0.6f)
        radius = 0.22f; // wide enough to read as a glowing ribbon, not a thread
    if (lifetime <= 0.0f)
        lifetime = 1.2f;

    // The packed 4-channel wisp sheet (energy_wisp.png) is the material's
    // SOURCE: R/G/B/A = coarse/fine/dissolve/turbulence. Unlike the smoke
    // sheet it is multi-channel by design, so the deform material can flow
    // internally; load it once and keep a layer that points at it.
    if (s_energyWispSheet.id == 0)
    {
        const VFX_SurfaceProfile *ep = VFX_SurfaceRegistry_Get(VFX_SURFACE_ENERGY_RIBBON);
        if (ep != NULL && ep->body.id > 0)
        {
            s_energyWispSheet = ep->body;
            SetTextureFilter(s_energyWispSheet, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_energyWispSheet, TEXTURE_WRAP_REPEAT);
        }
    }
    if (s_energyTrailLayer[0].texture == NULL)
    {
        s_energyTrailLayer[0] = (TrailLayer){
            .widthMul = 1.0f,
            .alphaMul = 1.0f,
            .whiten = 0.0f,
            .scrollMul = 1.0f,
            .headAlphaPow = 0.0f,
            .texture = (s_energyWispSheet.id > 0) ? &s_energyWispSheet : NULL,
        };
    }

    static bool s_energyTrailTuning = false;
    if (!s_energyTrailTuning)
    {
        // Meter-scale defaults, live-tunable via tuning.cfg (lazy-register on
        // first use — Tuning_Init runs after subsystem inits, see LANDMINES).
        Tuning_RegisterFloat("energytrail_amp_side",  &s_energyTrailAmpSide,  0.090f);
        Tuning_RegisterFloat("energytrail_amp_norm",  &s_energyTrailAmpNorm,  0.050f);
        Tuning_RegisterFloat("energytrail_freq",      &s_energyTrailFreq,     1.20f);
        Tuning_RegisterFloat("energytrail_speed",     &s_energyTrailSpeed,    4.50f);
        Tuning_RegisterFloat("energytrail_strength",  &s_energyTrailStrength, 0.22f);
        Tuning_RegisterFloat("energytrail_env_head",  &s_energyTrailEnvHead,  0.10f);
        Tuning_RegisterFloat("energytrail_env_tail",  &s_energyTrailEnvTail,  0.88f);
        Tuning_RegisterFloat("energytrail_wisp_mix",  &s_energyTrailWispMix,  0.35f);
        Tuning_RegisterFloat("energytrail_dissolve",  &s_energyTrailDissolve, 0.30f);
        Tuning_RegisterFloat("energytrail_turb",      &s_energyTrailTurb,     0.50f);
        Tuning_RegisterFloat("energytrail_edge_tear", &s_energyTrailEdgeTear, 0.35f);
        Tuning_RegisterFloat("energytrail_tiling_x",  &s_energyTrailTilingX,  3.0f);
        Tuning_RegisterFloat("energytrail_pan_coarse", &s_energyTrailPanCoarse, 1.20f);
        Tuning_RegisterFloat("energytrail_pan_fine",  &s_energyTrailPanFine,  2.00f);
        s_energyTrailTuning = true;
    }

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Vector3 headPos = {followTransform->m12, followTransform->m13, followTransform->m14};

    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.shape = TRAIL_SHAPE_RIBBON;
    cfg.pos = headPos;
    cfg.thick = radius;
    cfg.life = 1.0e6f; // emission lives until stopped; the detached tail fades itself
    cfg.tint = VC_WithAlpha(m->glow, 255); // the hot glow colour, not the dim body — additive energy needs heat
    cfg.disableInnerCore = true; // the packed wisp IS the body — no white core
    cfg.blendMode = BLEND_ADDITIVE;
    cfg.useCustomBlendMode = true; // additive halo goes to the emission pass
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN; // thin head -> FULL tail; the tip evaporates in the FS
    cfg.layers = s_energyTrailLayer; // own packed sheet — NOT the smoke layer stack
    cfg.layerCount = 1;
    cfg.sampleHz = 30.0f;
    cfg.trailLength = lifetime * cfg.sampleHz;
    cfg.idleSpeed = 0.05f;
    cfg.teleportSpeed = 25.0f;
    cfg.smoothSpline = false;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.onUpdate = SmokeTrail_OnUpdate; // live-tuning copy, same as smoke

    // ── STAGE 3 — GPU vertex waves (trail_deform.vs): OFF ─────────────────
    // Deform 0 = the strip stays a flat, clean band. The user asked to kill
    // the wave ("tắt deform đi") — the curl noise read as rigid and hid the
    // material's torn edges. The uber shader still routes here because the
    // PACKED MATERIAL (material.mode = 1 below) needs the fragment stage;
    // the sin/curl tunables stay dormant for experiments.
    cfg.deform.mode = 0.0f; // flat ribbon — no vertex waves
    cfg.deform.ampA[0] = s_energyTrailAmpSide;
    cfg.deform.ampA[1] = s_energyTrailAmpSide * 0.5f;
    cfg.deform.ampA[2] = s_energyTrailAmpSide * 0.25f;
    cfg.deform.ampB[0] = s_energyTrailAmpNorm;
    cfg.deform.ampB[1] = s_energyTrailAmpNorm * 0.55f;
    cfg.deform.ampB[2] = s_energyTrailAmpNorm * 0.28f;
    cfg.deform.freq[0] = s_energyTrailFreq;
    cfg.deform.freq[1] = s_energyTrailFreq * 2.2f;
    cfg.deform.freq[2] = s_energyTrailFreq * 4.1f;
    cfg.deform.speed[0] = s_energyTrailSpeed;
    cfg.deform.speed[1] = s_energyTrailSpeed * 1.35f;
    cfg.deform.speed[2] = s_energyTrailSpeed * 1.85f;
    cfg.deform.phase = Random01() * 10.0f; // no two casts read identical
    cfg.deform.envHead = s_energyTrailEnvHead; // wave dies at the emitter
    cfg.deform.envTail = s_energyTrailEnvTail; // and calms at the tail
    cfg.deform.strength = s_energyTrailStrength;
    cfg.deform.curlScale = 0.7f; // ~1.4 m swirl features — a few rolls per trail, not a blizzard

    // ── STAGE 4 — packed 4-channel wisp material (trail_deform.fs) ────────
    cfg.material.mode = 1.0f; // packed wisp
    cfg.material.wispMix = s_energyTrailWispMix;
    cfg.material.dissolve = s_energyTrailDissolve;
    cfg.material.dissolveSoft = 0.18f;
    cfg.material.turbStrength = s_energyTrailTurb;
    cfg.material.tilingX = s_energyTrailTilingX;
    cfg.material.tilingY = 1.0f;
    cfg.material.panCoarse = s_energyTrailPanCoarse;
    cfg.material.panFine = s_energyTrailPanFine;
    // Edge tear: the dissolve threshold jitters with fine noise, strongest at
    // the band edges, so the outline frays ("rách") like real smoke.
    cfg.material.edgeTear = s_energyTrailEdgeTear;
    // Tail ramp in segment space: the band WIDENS all the way to the tail
    // (envelope SMOKE_WIDEN); only the last stretch evaporates here so the
    // smoke "tan biến" at the tip — no hard end band, and no thin tail.
    cfg.material.tailFadeA = 0.88f;
    cfg.material.tailFadeB = 1.0f;

    int id = SpawnTrailEntity(cfg);
    Trail_AttachToTransform(id, followTransform, (Vector3){0, 0, 0});
    return id;
}
