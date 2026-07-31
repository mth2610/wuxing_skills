// ── VFX_ComposeSmokeTrail — soft ribbon trail that curls upward as it drifts ──
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
    s_smokeTrailSheet = ResourceManager_LoadTexture("assets/textures/smoke_ribbon.png");
    s_smokeTrailFlowMap = ResourceManager_LoadTexture("assets/textures/smoke_ribbon_flow.png");
    s_smokeTrailMask = ResourceManager_LoadTexture("assets/textures/smoke_ribbon_mask.png");
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
