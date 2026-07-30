// ── VFX_ComposeSmokeTrail — soft ribbon trail that curls upward as it drifts ──
//
// Ribbon-trail authoring guide, mapped onto TrailConfig (trail_system.h):
//
//   1. RIBBON TRAIL  -> TRAIL_SHAPE_RIBBON + TRAIL_TYPE_FOLLOWER, camera-facing,
//                        attached to the moving head with Trail_AttachToTransform
//                        so the engine recomputes the tip every frame — no manual
//                        per-frame position feed needed.
//   2. WIDTH/ALPHA   -> TRAIL_WIDTH_ENVELOPE_TAPER_TAIL: wide+bright at the head,
//                        smooth taper to the tail, never a hard cutoff.
//   3. FLOW/NOISE    -> exactly ONE layer carries the texture (the body); the
//                        outer glow layer stays texture=NULL (LANDMINES:
//                        several textured layers at different scroll phases
//                        average into FLAT). uvMetresPerTile scrolls by metres
//                        of path travelled, not by emitter speed, so the flow
//                        reads the same whether the head is fast or slow.
//   4. SHADER FEEL   -> additive blend, a faint wide untextured glow BEHIND the
//                        textured body, low contrast, soft edges. No separate
//                        hot-core layer — pure smoke, no bright rim baked in.
//   5. CURL UPWARD    -> forceField = FORCE_PRESET_FIRE_UPDRAFT perturbs the
//                        cloth; nodeHomeSpring springs each node back toward
//                        where it was laid so the ribbon curls along the swept
//                        path instead of writhing free of its own trail
//                        (see trail_system.h's comment on nodeHomeSpring).
//
// TRAIL_TYPE_FOLLOWER never self-terminates — the CALLER MUST call
// KillTrail(id) when the effect ends (trail contract in API.md).
//
// Signature matches the prototype already declared in visual_composer.h:
//   int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
//                             float radius, float lifetime);
// Colour comes from VFX_Material(mat) (Composition rule, API.md) rather than a
// texture/tint argument. The body layer's noise texture is optional and set
// once, separately, via VFX_SmokeTrail_SetTexture() — see below.

static ForceField s_smokeTrailUpdraft;
static TrailLayer s_smokeTrailLayers[2];
static bool s_smokeTrailInit = false;

static float s_smokeTrailCurlStrength = 1.0f;  // 0 = straight ribbon, 1 = full updraft curl
static float s_smokeTrailScrollSpeed = 0.35f;  // tiles/sec — slow reads as smoke, fast reads as water

// Optional tileable flow/noise smoke texture for the body layer (point 3 of
// the guide). NULL (the default) means the body layer draws untextured —
// still a correctly shaped, tapered, curling ribbon, just without the drifting
// noise pattern. Set this once during init, mirroring the same "set once,
// shared by everything after" idiom as TrailSystem_SetGlobalTexture:
//
//     Texture2D smokeTex = ResourceManager_LoadTexture("resources/vfx/smoke_flow.png");
//     VFX_SmokeTrail_SetTexture(&smokeTex);
//
// The pointer must stay valid for as long as any smoke trail is alive (the
// TrailEntity only stores a pointer to the layer table, never a copy —
// trail_system.h's "caller-owned, must outlive the trail"), so back it with a
// static/global Texture2D, never a stack temporary.
static const Texture2D *s_smokeTrailTexture = NULL;

void VFX_SmokeTrail_SetTexture(const Texture2D *smokeTex)
{
    s_smokeTrailTexture = smokeTex;
    if (s_smokeTrailInit)
        s_smokeTrailLayers[1].texture = smokeTex; // body layer only — see InitShared
}

static void SmokeTrail_InitShared(void)
{
    if (s_smokeTrailInit)
        return;

    s_smokeTrailUpdraft = ForceField_CreatePreset(FORCE_PRESET_FIRE_UPDRAFT);

    // Layer 0 (backmost): a faint, wide, UNTEXTURED glow — the soft halo
    // around the body. No texture: this layer carries no structure.
    s_smokeTrailLayers[0] = (TrailLayer){
        .widthMul = 1.6f, .alphaMul = 0.35f, .whiten = 0.0f,
        .scrollMul = 0.6f, .headAlphaPow = 0.0f, .texture = NULL,
    };
    // Layer 1: the body. The ONLY layer with `texture` set — this is what
    // carries the flowing smoke pattern (point 3 of the guide).
    s_smokeTrailLayers[1] = (TrailLayer){
        .widthMul = 1.0f, .alphaMul = 1.0f, .whiten = 0.1f,
        .scrollMul = 1.0f, .headAlphaPow = 0.0f, .texture = s_smokeTrailTexture,
    };
    // No hot-core layer — the smoke reads as smoke, not as a projectile with
    // a glowing edge riding inside it. Add VFX_ComposeCoreGlow separately at
    // the call site if a distinct bright head is wanted.

    Tuning_RegisterFloat("smoketrail_curl_strength", &s_smokeTrailCurlStrength, 1.0f);
    Tuning_RegisterFloat("smoketrail_scroll_speed", &s_smokeTrailScrollSpeed, 0.35f);
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
// lifetime        : seconds a laid-down ribbon segment lives before fading.
// returns a trail handle. Caller MUST call KillTrail(handle) when the effect ends.
int VFX_ComposeSmokeTrail(const Matrix *followTransform, VC_MaterialId mat,
                          float radius, float lifetime)
{
    SmokeTrail_InitShared();
    if (radius <= 0.0f || radius > 0.6f)
        radius = 0.15f; // clamp obviously-wrong scale rather than render a degenerate ribbon
    if (lifetime <= 0.0f)
        lifetime = 1.2f; // smoke lingers longer than a typical energy trail

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Vector3 headPos = { followTransform->m12, followTransform->m13, followTransform->m14 };

    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.shape = TRAIL_SHAPE_RIBBON;
    cfg.pos = headPos; // seeds the first history node before attach kicks in
    cfg.thick = radius;
    cfg.life = lifetime;
    cfg.tint = m->soft;                                   // smoke tint, not the hot glow tone
    cfg.blendMode = BLEND_ADDITIVE;                       // additive glow, per the guide
    cfg.widthEnvelope = TRAIL_WIDTH_ENVELOPE_TAPER_TAIL;   // wide+bright head -> thin tail
    cfg.layers = s_smokeTrailLayers;
    cfg.layerCount = 2;
    cfg.uvMetresPerTile = radius * 6.0f;         // tile size scales with the trail's own width
    cfg.uvScrollSpeed = s_smokeTrailScrollSpeed; // slow drift reads as smoke, not water
    cfg.sampleHz = 30.0f;                        // fixed-rate nodes -> length independent of fps
    cfg.idleSpeed = 0.05f;                       // stop laying nodes once the head is basically still
    cfg.teleportSpeed = 25.0f;                   // a bigger jump cuts+restarts, no straight bridge
    cfg.smoothSpline = true;                     // no faceted kinks in a slow-curling ribbon
    cfg.priority = VFX_PRIORITY_LOW;

    // 5. CURL UPWARD. The updraft perturbs the swept path; nodeHomeSpring keeps
    // the ribbon sprung back toward where it was laid so it curls in place
    // instead of tearing itself loose from its own trail.
    if (s_smokeTrailCurlStrength > 0.0f)
    {
        cfg.forceField = &s_smokeTrailUpdraft;
        cfg.nodeHomeSpring = Math_Mix(0.05f, 0.35f, s_smokeTrailCurlStrength);
        cfg.nodeHomeMaxDev = 0.25f; // metres, ACROSS the path — the loose safety bound
        cfg.nodeOrderFrac = 0.45f; // stays < 0.5 -> no folded/pinched segments
    }

    int id = SpawnTrailEntity(cfg);
    Trail_AttachToTransform(id, followTransform, (Vector3){0, 0, 0});
    return id; // caller: KillTrail(id) when the effect ends
}
