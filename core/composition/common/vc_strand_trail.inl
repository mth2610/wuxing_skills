// ── VFX_ComposeStrandTrail — the sin-wave strand trail (RzFX technique) ──────
//
// This REPLACES the old VFX_ComposeSmokeTrail (deleted 03/08/2026): a plume of
// soft puffs on the classic CPU-cloth pipeline. The two lived in one file for a
// while, which is how this one ended up calling the smoke trail's shared init
// and silently wearing its updraft force field. The old one is gone rather than
// deprecated because keeping both on the VFX bench meant two similar-looking
// buttons, and the wrong one kept getting judged (see docs/PROGRESS.md).
// VFX_STRAND_SMOKE is the smoke style here.
//
// Implements the breakdown at ryanzengvfx.blogspot.com (2019-04). The three
// stages and the reasons they are where they are:
//
//   1. POINT SAMPLER (CPU)  -> TRAIL_TYPE_FOLLOWER history ring at a fixed
//                              sampleHz. Engine-owned, no malloc.
//   2. RIBBON BUILDER       -> TRAIL_SHAPE_RIBBON, camera-facing. The strip is
//                              a FLAT WIDE QUAD and stays one: the wave never
//                              touches geometry, so it cannot fold the strip
//                              through itself on a tight turn.
//   3. FRAGMENT MATERIAL    -> trail_deform.fs mode 2. Wave, strands, flow
//                              warp, dissolve and the HDR ramp all live here.
//
// Release with VFX_StrandTrail_Stop(handle) (detaches the head, lets the laid
// ribbon dissolve) or KillTrail(handle) (immediate cut).

// ── WHY THE WAVE IS WHERE IT IS ──────────────────────────────────────────────
//
// The wave lives in the FRAGMENT stage (material mode 2), not in the vertex
// stage. Vertex deform is deliberately off: displacing the strip made it fold
// on itself and, because the displacement was a function of NORMALIZED
// segment, the whole waveform stretched every time the trail grew or the head
// changed speed — that is what read as a rigid swinging rope. In UV space the
// trail snakes inside a wide flat quad, anchored in METRES of laid path, so the
// crests stand still on the trail and only time walks them toward the tail.
//
// The trail is THREE strand bundles crossing each other, not one band: the
// hairs come from the sheet's own filament density
// (scripts/gen_energy_wisp_texture.py), and everything that frays is ramped by
// the along-trail coordinate, so the head stays a tight ribbon and the tail
// fans apart. The quad is ~3x wider than what you see, because waveAmp +
// bundleWidth is the room the bundles swing in.
//
// STYLES ARE DATA. A bright energy filament and a heavy smoke plume are the
// same shader with different numbers — mainly strandGain (>1 thins the hairs
// into wires, <1 fattens them until they merge into mass), bundleWidth (which
// magnifies the sheet's hairs in world space), flowStrength, and the blend /
// bodyOpacity pair that decides whether the trail glows or occludes. Add a row,
// never a second composer.
typedef struct
{
    const char *tuningPrefix; // tuning.cfg key prefix, e.g. "energytrail"
    VFX_SurfaceId surface;    // the strand sheet this style is authored against.
                              // NOT shared: strand density lives in the asset,
                              // so a style whose look needs 300 thin hairs
                              // cannot borrow a sheet that has 34 thick ones.
    float radiusDefault;      // quad half-width in metres when the caller passes junk
    float waveAmp, waveFreq, waveTravel, waveSpread;
    float bundleWidth, edgeSoft;
    float hdrGain, strandGain, flowStr, bundleWt;
    float dissolve, dissolveSoft;
    float envHead, wispMix;
    float tilingX, panCoarse, panFine;
    float bodyOpacity;        // how much the BLEND_ALPHA body pass occludes
    float hotWhiten;          // legacy tuning name: 0 = base, 1 = material hotGrad
    float tailDarken;         // 0 = tail keeps the head colour, 1 = tail goes
                              // to the material's darkest tone. The along-trail
                              // ramp: energy cools, smoke thins to grey.
    unsigned char tintAlpha;
    bool useGlowTint;         // true = VFX_Material glow (hot), false = body (dim)
    bool additive;            // false = the trail occludes instead of glowing
    TrailWidthEnvelopeType widthEnvelope;
    float tailFadeA;
    // How the style's sheet is authored, and therefore how it must be sampled.
    // false = repeating filament material, tiled by metres (tilingX).
    // true  = ONE complete trail shape with its head/tail taper painted in,
    //         stretched once over the whole trail. Getting this wrong does not
    //         degrade the look, it destroys it: a tiled shape sheet is a rope
    //         of identical segments, and a stretched material sheet is a smear.
    bool stretchUV;
    // Tail treatment — see TrailMaterialConfig. Staggering the three bundles'
    // ends is what stops the trail finishing on a line; the other two decide
    // whether it breaks up (dissolve) and whether it thins first (narrow).
    float tailStagger, tailDissolve, tailNarrow;
} StrandTrailStyle;

static StrandTrailStyle s_strandStyles[VFX_STRAND_STYLE_COUNT] = {
    // ── ENERGY ── thin bright filaments with hot cores, lifted into HDR so
    // post-FX bloom picks them up. Trails run BOTH passes (main.c calls
    // DrawTrailEntitiesBody then DrawTrailEntitiesEmission): bodyOpacity is
    // high so the coloured body still holds hue over a bright destination,
    // while the emission pass carries the HDR hot core. trail_deform.fs
    // compacts the filament mask before the shared compositor, which keeps the
    // gaps between hairs without making the whole support pale.
    [VFX_STRAND_ENERGY] = {
        .tuningPrefix = "energytrail",
        .surface = VFX_SURFACE_ENERGY_RIBBON,
        .radiusDefault = 0.45f,
        .waveAmp = 0.40f, .waveFreq = 0.55f, .waveTravel = 0.85f, .waveSpread = 0.65f,
        .bundleWidth = 0.34f, .edgeSoft = 0.18f,
        .hdrGain = 1.85f, .strandGain = 1.35f, .flowStr = 0.55f, .bundleWt = 0.80f,
        .dissolve = 0.55f, .dissolveSoft = 0.22f,
        .envHead = 0.10f, .wispMix = 0.70f,
        .tilingX = 0.65f, .panCoarse = 0.35f, .panFine = 0.70f,
        .bodyOpacity = 0.90f, .hotWhiten = 0.72f, .tailDarken = 0.45f,
        .tintAlpha = 255, .useGlowTint = true, .additive = true,
        .widthEnvelope = TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE, .tailFadeA = 0.72f,
        .stretchUV = false, // energy_wisp.png is a repeating filament material
        // A filament trail should end as a few surviving threads, so: a modest
        // stagger, light extra dissolve, strong narrowing.
        .tailStagger = 0.13f, .tailDissolve = 0.20f, .tailNarrow = 0.55f,
    },
    // ── SMOKE ── many thin faint strands piling up into mass, occluding.
    //
    // The ASSET is the load-bearing difference, and not in the way the first
    // attempt assumed. smoke_strand.png is NOT a denser filament material: it
    // is one complete smoke streak, thick through the middle and tapering to
    // nothing at both ends, with a few curls rolling through it — the shape the
    // reference sheet's R/G channels actually hold. It is stretched once over
    // the trail (stretchUV), never tiled. Three sin-offset samples of a WISP
    // overlap into smoke; three sin-offset samples of a tiled filament sheet
    // are three ropes, which is exactly what the first two attempts produced.
    //
    // additive OFF with bodyOpacity near 1: smoke must hide what is behind it.
    // An additive plume is a glow, whatever colour you give it. Everything else
    // slows down — smoke drifts, energy races.
    [VFX_STRAND_SMOKE] = {
        .tuningPrefix = "smoketrailfx",
        .surface = VFX_SURFACE_SMOKE_STRAND,
        .radiusDefault = 0.70f,
        .waveAmp = 0.30f, .waveFreq = 0.35f, .waveTravel = 0.30f, .waveSpread = 0.85f,
        .bundleWidth = 0.62f, .edgeSoft = 0.34f,
        .hdrGain = 1.0f, .strandGain = 0.75f, .flowStr = 0.65f, .bundleWt = 0.85f,
        .dissolve = 0.40f, .dissolveSoft = 0.45f,
        .envHead = 0.06f, .wispMix = 0.60f, // R and G are two DIFFERENT wisps — blend them
        .tilingX = 0.30f, .panCoarse = 0.12f, .panFine = 0.22f,
        .bodyOpacity = 0.96f, .hotWhiten = 0.0f, .tailDarken = 0.35f,
        .tintAlpha = 205, .useGlowTint = false, .additive = false,
        .widthEnvelope = TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN,
        // The sheet already tapers both ends, so the material ramp only
        // takes the very tip — fading twice thins the plume to nothing.
        .tailFadeA = 0.94f,
        .stretchUV = true, // smoke_strand.png is one whole wisp, head to tail
        // Smoke should come apart over a LONGER stretch and break into islands
        // rather than thin to threads: wider stagger, heavier dissolve, and
        // less narrowing than the energy style (a plume stays broad as it goes).
        .tailStagger = 0.22f, .tailDissolve = 0.40f, .tailNarrow = 0.78f,
    },
};

// Mode-1 compatibility slot; mode 2 derives the across scale from bundleWidth.
static float s_strandTilingY = 1.00f;

// One packed sheet + one layer PER STYLE (R/G filament patterns, B flow
// distortion, A dissolve). Sheets come from the surface registry and the layer
// owns a pointer, so trails stay engine-owned (no per-trail texture handles).
static Texture2D s_strandSheet[VFX_STRAND_STYLE_COUNT] = {0};
static TrailLayer s_strandLayer[VFX_STRAND_STYLE_COUNT][1] = {0};

// Which style each live trail was spawned with, so the per-frame tuning copy
// pushes the RIGHT row back. Without it a scene holding both a smoke plume and
// an energy filament would have whichever spawned last overwrite the other.
static signed char s_strandTrailStyleOf[MAX_TRAIL_PARTICLES];
static signed char s_strandTrailMatOf[MAX_TRAIL_PARTICLES];

// tuning.cfg override: -1 = every trail keeps the style it was spawned with,
// 0/1 = force that row onto ALL live strand trails. The per-frame copy already
// rewrites the whole material, and blend mode / tint / width envelope are read
// per frame by the renderer too, so a style swap is complete and live — which
// is the only way to A/B two styles on one bench button (the VFX bench allows
// exactly one entry per source .inl).
static float s_strandStyleOverride = -1.0f;

// Loads a style's strand sheet from the surface registry and builds its layer,
// once. Split out of the composer because a LIVE style swap has to be able to
// call it too: the layer (and therefore the texture) is normally chosen at
// spawn, so without this the smoke style would keep rendering the energy
// sheet's 34 thick hairs and look exactly like the style it replaced — which is
// indistinguishable from "the style knob does nothing".
static void StrandTrail_EnsureSheet(int styleIdx)
{
    const StrandTrailStyle *st = &s_strandStyles[styleIdx];
    if (s_strandSheet[styleIdx].id == 0)
    {
        const VFX_SurfaceProfile *ep = VFX_SurfaceRegistry_Get(st->surface);
        if (ep != NULL && ep->body.id > 0)
        {
            s_strandSheet[styleIdx] = ep->body;
            SetTextureFilter(s_strandSheet[styleIdx], TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_strandSheet[styleIdx], TEXTURE_WRAP_REPEAT);
        }
        else
        {
            TraceLog(LOG_WARNING, "STRAND TRAIL: sheet for style '%s' missing — "
                                  "the strands have nowhere to come from",
                     st->tuningPrefix);
        }
    }
    if (s_strandLayer[styleIdx][0].texture == NULL)
    {
        s_strandLayer[styleIdx][0] = (TrailLayer){
            .widthMul = 1.0f,
            .alphaMul = 1.0f,
            .whiten = 0.0f,
            .scrollMul = 1.0f,
            .headAlphaPow = 0.0f,
            .texture = (s_strandSheet[styleIdx].id > 0) ? &s_strandSheet[styleIdx] : NULL,
        };
    }
}

static void StrandTrail_EnsureTuning(int styleIdx); // defined below

static void StrandTrail_OnUpdate(int trailId, float dt)
{
    (void)dt;
    TrailEntity *trail = GetTrail(trailId);
    if (trail == NULL || !trail->active)
        return;
    if (trailId < 0 || trailId >= MAX_TRAIL_PARTICLES)
        return;
    int styleIdx = s_strandTrailStyleOf[trailId];
    if (styleIdx < 0 || styleIdx >= VFX_STRAND_STYLE_COUNT)
        return;
    const int askedStyle = styleIdx;
    if (s_strandStyleOverride >= -0.5f)
    {
        int forced = (int)(s_strandStyleOverride + 0.5f);
        if (forced >= 0 && forced < VFX_STRAND_STYLE_COUNT)
        {
            styleIdx = forced;
            StrandTrail_EnsureTuning(styleIdx);
            StrandTrail_EnsureSheet(styleIdx);
            // A/B knobs persist in tuning.cfg across sessions and outlive the
            // comparison they were set for. A stale `strandtrail_style = 1`
            // silently substitutes the SMOKE row (no hot core, body tint, no
            // additive) for every ENERGY trail — which reads on screen as "the
            // energy trail is a flat red band" and sends the next reader into
            // the shader. WARNING, not INFO, and it names the file and the
            // style that was ASKED for: the line below only says which row won.
            static int s_lastForcedPair = -1;
            if (s_lastForcedPair != askedStyle * 16 + styleIdx)
            {
                s_lastForcedPair = askedStyle * 16 + styleIdx;
                if (styleIdx != askedStyle)
                    TraceLog(LOG_WARNING,
                             "STRAND TRAIL: tuning.cfg strandtrail_style=%.1f is FORCING "
                             "'%s' over the requested '%s' — set it to -1 to honour the "
                             "style each trail was spawned with",
                             (double)s_strandStyleOverride,
                             s_strandStyles[styleIdx].tuningPrefix,
                             s_strandStyles[askedStyle].tuningPrefix);
            }
        }
        else
        {
            // A silent out-of-range override is indistinguishable from "the
            // knob does nothing" — which is exactly how it gets reported.
            static float s_lastBadOverride = -999.0f;
            if (s_strandStyleOverride != s_lastBadOverride)
            {
                s_lastBadOverride = s_strandStyleOverride;
                TraceLog(LOG_WARNING,
                         "STRAND TRAIL: strandtrail_style=%.1f out of range (-1..%d) — ignored",
                         (double)s_strandStyleOverride, VFX_STRAND_STYLE_COUNT - 1);
            }
        }
    }
    // Log on CHANGE, not once at startup: tuning.cfg hot-reloads, so a one-shot
    // line scrolls away long before the edit that matters arrives.
    {
        static int s_lastLoggedStyle = -2;
        if (styleIdx != s_lastLoggedStyle)
        {
            s_lastLoggedStyle = styleIdx;
            TraceLog(LOG_INFO, "STRAND TRAIL: style -> %s (%s, bodyOpacity %.2f)",
                     s_strandStyles[styleIdx].tuningPrefix,
                     s_strandStyles[styleIdx].additive ? "additive" : "alpha/occluding",
                     (double)s_strandStyles[styleIdx].bodyOpacity);
        }
    }
    const StrandTrailStyle *st = &s_strandStyles[styleIdx];

    // The three fields the RENDERER reads per frame, not just at spawn. They
    // have to be re-pushed here or a live style swap would change the material
    // and leave an additive glow where an occluding plume was asked for.
    trail->blendMode = st->additive ? BLEND_ADDITIVE : BLEND_ALPHA;
    trail->widthEnvelope = st->widthEnvelope;
    // The SHEET, not just the numbers. Each style is authored against its own
    // strand density; swapping the parameters while leaving the other style's
    // texture bound produces neither look.
    trail->layers = s_strandLayer[styleIdx];
    trail->layerCount = 1;
    {
        const VFX_ElementMaterial *lm = VFX_Material((VC_MaterialId)s_strandTrailMatOf[trailId]);
        Color lbase = st->useGlowTint ? lm->glow : lm->body;
        trail->tint = VC_WithAlpha(lbase, st->tintAlpha);
        // The material hot gradient carries hue-aware heat (Fire is gold,
        // Lightning is blue-white, etc.). Whitening a red glow produces pink,
        // not a yellow core, and discards the source of truth already authored
        // in VFX_ElementMaterial.
        Color hotTarget = ColorGradient_Sample(lm->hotGrad, 0.20f);
        trail->material.hotColor = ColorLerp(lbase, hotTarget, st->hotWhiten);
        trail->material.hotColor.a = 255;
        trail->material.tailColor = (Color){
            (unsigned char)(lbase.r * (1.0f - st->tailDarken) + lm->body.r * st->tailDarken),
            (unsigned char)(lbase.g * (1.0f - st->tailDarken) + lm->body.g * st->tailDarken),
            (unsigned char)(lbase.b * (1.0f - st->tailDarken) + lm->body.b * st->tailDarken), 255};
    }
    trail->material.tailFadeA = st->tailFadeA;

    trail->forceField = NULL;
    trail->nodeHomeSpring = 0.0f;
    trail->nodeHomeMaxDev = 0.0f;
    trail->nodeOrderFrac = 0.0f;
    trail->useFlowMap = false;

    trail->material.waveAmp = st->waveAmp;
    trail->material.waveFreq = st->waveFreq;
    trail->material.waveTravel = st->waveTravel;
    trail->material.waveSpread = st->waveSpread;
    trail->material.bundleWidth = st->bundleWidth;
    trail->material.edgeSoft = st->edgeSoft;
    trail->material.hdrGain = st->hdrGain;
    trail->material.strandGain = st->strandGain;
    trail->material.flowStrength = st->flowStr;
    trail->material.bundleWeight = st->bundleWt;
    trail->material.stretchUV = st->stretchUV ? 1.0f : 0.0f;
    trail->material.tailStagger = st->tailStagger;
    trail->material.tailDissolve = st->tailDissolve;
    trail->material.tailNarrow = st->tailNarrow;
    trail->material.dissolve = st->dissolve;
    trail->material.dissolveSoft = st->dissolveSoft;
    trail->material.wispMix = st->wispMix;
    trail->material.tilingX = st->tilingX;
    trail->material.tilingY = s_strandTilingY;
    trail->material.panCoarse = st->panCoarse;
    trail->material.panFine = st->panFine;
    trail->material.bodyOpacity = st->bodyOpacity;
    trail->deform.envHead = st->envHead;
}

// Registers one style's knobs under its own prefix, once, on first use of that
// style. Lazy and per-style because Tuning_Init runs after the subsystem inits
// (registering early silently keeps the default — see docs/LANDMINES.md) and
// because TUNING_MAX_VALUES is finite: a style nobody spawns costs nothing.
static void StrandTrail_EnsureTuning(int styleIdx)
{
    static bool s_registered[VFX_STRAND_STYLE_COUNT] = {false};
    if (s_registered[styleIdx])
        return;
    s_registered[styleIdx] = true;

    StrandTrailStyle *st = &s_strandStyles[styleIdx];
    char key[64];
    const char *p = st->tuningPrefix;
#define STRAND_TUNE(suffix, field)                                      \
    do {                                                                \
        snprintf(key, sizeof(key), "%s_%s", p, suffix);                 \
        Tuning_RegisterFloat(key, &st->field, st->field);               \
    } while (0)
    STRAND_TUNE("wave_amp", waveAmp);
    STRAND_TUNE("wave_freq", waveFreq);
    STRAND_TUNE("wave_travel", waveTravel);
    STRAND_TUNE("wave_spread", waveSpread);
    STRAND_TUNE("bundle_width", bundleWidth);
    STRAND_TUNE("edge_soft", edgeSoft);
    STRAND_TUNE("hdr_gain", hdrGain);
    STRAND_TUNE("strand_gain", strandGain);
    STRAND_TUNE("flow_str", flowStr);
    STRAND_TUNE("bundle_wt", bundleWt);
    STRAND_TUNE("dissolve", dissolve);
    STRAND_TUNE("dissolve_soft", dissolveSoft);
    STRAND_TUNE("env_head", envHead);
    STRAND_TUNE("wisp_mix", wispMix);
    STRAND_TUNE("tiling_x", tilingX);
    STRAND_TUNE("pan_coarse", panCoarse);
    STRAND_TUNE("pan_fine", panFine);
    STRAND_TUNE("body_opacity", bodyOpacity);
    STRAND_TUNE("tail_stagger", tailStagger);
    STRAND_TUNE("tail_dissolve", tailDissolve);
    STRAND_TUNE("tail_narrow", tailNarrow);
    STRAND_TUNE("tail_fade", tailFadeA);
#undef STRAND_TUNE

    // One shared knob, registered with the first style that spawns: -1 keeps
    // each trail's own style, 0/1 forces one onto every live strand trail.
    Tuning_RegisterFloat("strandtrail_style", &s_strandStyleOverride, -1.0f);
}

// followTransform : the moving head this trail chases. Caller owns the Matrix
//                    and must keep it valid for the trail's lifetime.
// radius          : the QUAD half-width in metres — the room the waves swing
//                    in, NOT the thickness you see. Pass <= 0 for the style's
//                    own default; anything over 1.2 m is treated as a mistake
//                    and clamped, because a strip wider than the path it sweeps
//                    renders as a blob whose width curls instead of a trail
//                    whose length does.
// lifetime        : seconds of trail memory at the authored sample rate.
int VFX_ComposeStrandTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float radius, float lifetime, VFX_StrandStyle style)
{
    int styleIdx = (int)style;
    if (styleIdx < 0 || styleIdx >= VFX_STRAND_STYLE_COUNT)
        styleIdx = (int)VFX_STRAND_ENERGY;
    StrandTrail_EnsureTuning(styleIdx);
    const StrandTrailStyle *st = &s_strandStyles[styleIdx];

    if (radius <= 0.0f || radius > 1.2f)
        radius = st->radiusDefault;
    if (lifetime <= 0.0f)
        lifetime = 1.2f;

    // The strand sheet (energy_wisp.png) is the material's SOURCE: R/G are
    // filament patterns, B is flow distortion, A is dissolve. Load it once and
    // keep a layer that points at it, so trails stay engine-owned.
    StrandTrail_EnsureSheet(styleIdx);

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Color base = st->useGlowTint ? m->glow : m->body;
    Vector3 headPos = {followTransform->m12, followTransform->m13, followTransform->m14};

    TrailConfig cfg = {0};
    cfg.type = TRAIL_TYPE_FOLLOWER;
    cfg.shape = TRAIL_SHAPE_RIBBON;
    cfg.pos = headPos;
    cfg.thick = radius;
    cfg.life = 1.0e6f; // emission lives until stopped; the detached tail fades itself
    cfg.tint = VC_WithAlpha(base, st->tintAlpha);
    cfg.disableInnerCore = true; // the strands ARE the body — no separate white core
    cfg.blendMode = st->additive ? BLEND_ADDITIVE : BLEND_ALPHA;
    // BLEND_ALPHA is enum value 0. Without this flag the legacy trail fallback
    // treats zero as "unspecified" and silently draws additive — which for the
    // smoke style would turn an occluding plume back into a glow.
    cfg.useCustomBlendMode = true;
    cfg.widthEnvelope = st->widthEnvelope;
    cfg.layers = s_strandLayer[styleIdx];
    cfg.layerCount = 1;
    cfg.sampleHz = 30.0f;
    cfg.trailLength = lifetime * cfg.sampleHz;
    cfg.idleSpeed = 0.05f;
    cfg.teleportSpeed = 25.0f;
    cfg.smoothSpline = false;
    cfg.priority = VFX_PRIORITY_LOW;
    cfg.onUpdate = StrandTrail_OnUpdate; // strand tunables only — never the smoke cloth

    // ── GPU vertex waves (trail_deform.vs): OFF, and staying off ──────────
    // Every version of this effect that displaced the VERTICES read as a
    // rigid rope: the strip folds through itself on a tight turn, and the
    // displacement was a function of NORMALIZED segment, so the whole
    // waveform stretched and slid whenever the trail grew or the head changed
    // speed. The wave belongs in the fragment stage (material mode 2 below),
    // where it is arc-length anchored and cannot touch the geometry. The uber
    // shader still routes here — the router key is deform OR material.
    cfg.deform.mode = 0.0f;
    cfg.deform.envHead = st->envHead; // shared with the FS disorder ramp
    cfg.deform.envTail = 0.99f;
    cfg.deform.phase = Random01() * 10.0f; // no two casts read identical

    // ── SIN-WAVE STRAND TRAIL (trail_deform.fs, material mode 2) ──────────
    cfg.material.mode = 2.0f;
    cfg.material.waveAmp = st->waveAmp;
    cfg.material.waveFreq = st->waveFreq;
    cfg.material.waveTravel = st->waveTravel;
    cfg.material.waveSpread = st->waveSpread;
    cfg.material.bundleWidth = st->bundleWidth;
    cfg.material.edgeSoft = st->edgeSoft;
    cfg.material.hdrGain = st->hdrGain;
    cfg.material.strandGain = st->strandGain;
    cfg.material.flowStrength = st->flowStr;
    cfg.material.bundleWeight = st->bundleWt;
    cfg.material.stretchUV = st->stretchUV ? 1.0f : 0.0f;
    cfg.material.tailStagger = st->tailStagger;
    cfg.material.tailDissolve = st->tailDissolve;
    cfg.material.tailNarrow = st->tailNarrow;
    cfg.material.dissolve = st->dissolve;
    cfg.material.dissolveSoft = st->dissolveSoft;
    // The core colour comes from the material's authored hot gradient. For
    // Fire this moves orange-red toward gold instead of toward pink-white.
    Color hotTarget = ColorGradient_Sample(m->hotGrad, 0.20f);
    cfg.material.hotColor = ColorLerp(base, hotTarget, st->hotWhiten);
    cfg.material.hotColor.a = 255;
    // Along-trail ramp (the reference's Phase 6): the head is cfg.tint, the
    // tail is this. Toward the material's own dark body tone rather than toward
    // black — a trail that ramps to black reads as dirt, not as cooling.
    cfg.material.tailColor = (Color){
        (unsigned char)(base.r * (1.0f - st->tailDarken) + m->body.r * st->tailDarken),
        (unsigned char)(base.g * (1.0f - st->tailDarken) + m->body.g * st->tailDarken),
        (unsigned char)(base.b * (1.0f - st->tailDarken) + m->body.b * st->tailDarken), 255};
    cfg.material.wispMix = st->wispMix;
    cfg.material.tilingX = st->tilingX;
    cfg.material.tilingY = s_strandTilingY;
    cfg.material.panCoarse = st->panCoarse;
    cfg.material.panFine = st->panFine;
    // How much of this trail survives into the BLEND_ALPHA body pass. Anything
    // that must stay readable over a bright destination needs this above zero —
    // additive alone cannot hold contrast against a bright background.
    cfg.material.bodyOpacity = st->bodyOpacity;
    cfg.material.contrastProfile =
        styleIdx == (int)VFX_STRAND_SMOKE
            ? VFX_CONTRAST_SMOKE
            : VFX_CONTRAST_ENERGY;
    cfg.material.tailFadeA = st->tailFadeA;
    cfg.material.tailFadeB = 1.0f;

    int id = SpawnTrailEntity(cfg);
    if (id >= 0 && id < MAX_TRAIL_PARTICLES)
    {
        s_strandTrailStyleOf[id] = (signed char)styleIdx;
        s_strandTrailMatOf[id] = (signed char)mat;
    }
    Trail_AttachToTransform(id, followTransform, (Vector3){0, 0, 0});
    return id;
}

int VFX_ComposeEnergyTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float radius, float lifetime)
{
    return VFX_ComposeStrandTrail(followTransform, mat, radius, lifetime,
                                  VFX_STRAND_ENERGY);
}

int VFX_ComposeSmokeStrandTrail(const Matrix *followTransform, VC_MaterialId mat,
                                float radius, float lifetime)
{
    return VFX_ComposeStrandTrail(followTransform, mat, radius, lifetime,
                                  VFX_STRAND_SMOKE);
}

// Ends emission while preserving the laid ribbon so it drifts and dissolves on
// its own. Detaching keeps the history; KillTrail() pops it out of existence.
void VFX_StrandTrail_Stop(int trailId)
{
    Trail_AttachToTransform(trailId, NULL, (Vector3){0.0f, 0.0f, 0.0f});
}
