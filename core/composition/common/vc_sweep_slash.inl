// ── E6.5 — VFX_ComposeSweepSlash ─────────────────────────────────────────────
//
// A weapon-art sweep: the arc a blade leaves behind, not a glowing banana.
//
// WHAT THE SPEC ASKED FOR AND WHAT IT GOT. The spec wants "an arc mesh driven by
// an authored flipbook mask (`arc_slash_*`) plus refraction behind it". The mask
// does not exist and nothing in `scripts/flipbook/` bakes one — that pipeline is
// a Taichi smoke SIMULATION, which is the wrong tool for a blade streak. So this
// takes the route §0.2 explicitly authorises where an asset stalls: the mask is
// GENERATED (precedent: `VFX_ComposeDissolveExit`'s body texture, and
// `VFX_ComposeEnergySmoke` before it). It is a real texture with real striations,
// not "procedural" in the sense the spec rejects — the thing it rejects is
// procedural GEOMETRY, the `SlashArc`/`GustSlash` family of hand-rolled quads
// with no mask at all. Swapping in an authored sheet later is one line in
// `SweepSlash_InitShared`.
//
// THE THREE THINGS THAT MAKE IT READ AS A CUT:
//   1. A HEAD THAT OUTRUNS ITS TAIL. The band is the gap between two angles
//      travelling the same arc at different rates. It is never a full arc that
//      fades — a slash you can see all of at once is a decoration.
//   2. AN ASYMMETRIC CROSS-SECTION. The mask is near-white against its OUTER
//      edge (the blade's own path) and smears inward. Symmetric = a tube.
//   3. A NEEDLE AT THE TIP. Width goes to zero at the head, so the leading end
//      is a point rather than a cut-off rectangle. This is the single biggest
//      difference between "slash" and "ribbon".
//
// The strip is `ribbon_strip.h` in RIBBON_FIXED_NORMAL (the slash lies in ONE
// plane — a camera-facing ribbon would twist the arc as the camera moves) with
// `Ribbon_ComputeArcLengthUV`, so the mask keeps its texel density as the band
// shortens instead of being squeezed unevenly by an index/count UV.

#include "core/tuning.h"

#define SWEEP_SLASH_POINTS 24

static Texture2D  s_slashTex     = {0};   // edge mask: rim + smear
static Texture2D  s_slashHaloTex = {0};   // the same smear with NO rim
static SkillCurve s_slashFade = {0};
static bool       s_slashInit = false;

static float s_slashWidth   = 1.0f;   // x on half-width
static float s_slashTilt    = 0.45f;  // plane tilt off horizontal, radians
static float s_slashDistort = 1.0f;   // 0 = no refraction (perf switch)
static float s_slashSparks  = 1.0f;   // x on spark rate, 0 = none

// Emission accumulators. STATIC, i.e. shared by every concurrent slash — which
// is correct in aggregate (two slashes advance it twice as fast and emit twice
// as often) and only approximate per-slash (a given spark may be attributed to
// the other one's arc). The alternative is per-call-site state, which this
// signature has nowhere to keep.
static float s_slashSparkAcc   = 0.0f;
static float s_slashDistortAcc = 0.0f;

#define SLASH_SWEEP_T     0.34f   // t01 at which the HEAD reaches the far end
#define SLASH_TAIL_LAG    0.16f   // t01 at which the TAIL starts moving
#define SLASH_TAIL_END    1.00f   // t01 at which the tail catches up (band = 0)
// SPARK RATE, corrected from the first screenshot (28/07/2026). At 55/s across a
// 0.54 s sweep with a 0.25 s average life, ~30 sparks were alive at once, spread
// over the whole band and living long enough to sit still on it: the slash read
// as A STRING OF FAIRY LIGHTS with a faint smear behind. Three separate mistakes
// compounded, and the rate was only the loudest:
//   - too many, too long-lived (they accumulate into a chain instead of trailing);
//   - stretch 0.10 gives 1 + 4*0.10 = 1.4x at 4 m/s, i.e. a ROUND DOT. A streak
//     needs the factor near 4-6, so the strength has to be near 1;
//   - emissiveBoost 1.5 on white puts each dot over the bloom threshold, and
//     bloom turns a 2 cm sprite into a 10 cm bead.
// Sparks are the grit the edge throws. If they are countable, there are too many.
#define SLASH_SPARK_RATE  24.0f   // x ~0.14 s life ~ 3 live
#define SLASH_DISTORT_RATE 9.0f   // refraction sources/sec, x0.16 s life ~ 1.5 live

// The mask. u (x) runs ACROSS the blade, v (y) runs ALONG the sweep.
//
// WHICH SIDE IS u = 0, and it is not a matter of taste. DrawRibbonStripEx emits
// u = 0 at `center + side*halfWidth` and u = 1 at `center - side*halfWidth`,
// with `side = normalize(cross(tangent, normal))`. For this arc that cross
// product works out to the radially OUTWARD direction (basis check: axU x axV =
// n, so tangent x n = axU cos a + axV sin a = the radial), which makes **u = 0
// the outer edge** — the one the blade itself traced — and u = 1 the inner one.
// Get this backwards and the hot rim ends up buried against the pivot with the
// smear hanging outside the swing, which looks like a mistake but reads on
// screen as merely "a bit soft".
//
// Note also that the arc path is the strip's CENTRE (u = 0.5), so the band's
// outer boundary is at radius `length + halfWidth`, not `length`.
//
// Generated once, on the CPU, so the `fract(sin())` Mali hash landmine
// (ENGINE_LANDMINES §4) does not apply — but there is no hash here anyway: every
// term is closed-form, which is also what keeps the striations from crawling.
// The mask's cross-blade profile at `u`. `withRim` = the blade's own edge; the
// HALO pass passes false and gets the smear alone.
//
// WHY THE HALO MUST NOT CARRY THE RIM. All three passes share one outer edge
// (below), so a halo that also has a rim lays a rim 1.8x THICKER in metres on
// exactly the same line — the edge stops being a line and becomes a broad soft
// gradient. That is the "crescent moon, not a cut" the third screenshot showed:
// the geometry was right by then and the edge was still being washed out, by the
// pass whose whole job is to be soft.
static float SweepSlash_Profile(float u, bool withRim)
{
    float e = 1.0f - u;                     // "outerness"; u = 0 is the OUTER edge
    if (!withRim)
        return powf(e, 1.7f) * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));

    // ① The smear. Everything behind the edge, falling off fast inward.
    float body = powf(e, 2.4f);
    // ② The edge itself: a hot line just INSIDE the border, not on it — on the
    //    border it would be half-cut by the AA shoulder below.
    float d    = u / 0.05f;
    float rim  = expf(-d * d);
    float prof = 0.40f * body + 0.72f * rim;
    // ③ A 1.5% shoulder at the outer border. The edge must LOOK hard and must
    //    not BE hard: a mask that goes 1 → 0 between two texels aliases into a
    //    staircase the moment the arc is seen edge-on.
    //    THE SHOULDER MUST BE NARROWER THAN THE RIM IS FAR FROM THE BORDER. At
    //    4% against a rim sigma of 0.05 the two overlapped and the shoulder was
    //    halving the rim's peak — the AA meant to protect the edge was erasing
    //    it. 1.5% of 192 texels is ~3, which is all antialiasing needs.
    return prof * (1.0f - SmoothStep01((e - 0.985f) / 0.015f));
}

// Two sheets from one loop: the edge mask (body + core passes) and the rim-less
// halo. `W` is 192, not 64: the rim is 5% of the strip and the strip is metres
// across on screen, so at 64 the hot line was three texels magnified into a
// gradient — bilinear cannot invent an edge that the texture does not resolve.
static void SweepSlash_BuildMask(void)
{
    const int W = 192;   // across the blade — must RESOLVE the rim, not blur it
    const int H = 128;   // along the sweep — carries the striations
    Image img  = GenImageColor(W, H, BLANK);
    Image halo = GenImageColor(W, H, BLANK);
    for (int y = 0; y < H; y++)
    {
        float v = ((float)y + 0.5f) / (float)H;
        for (int x = 0; x < W; x++)
        {
            float u = ((float)x + 0.5f) / (float)W;   // 0 = OUTER edge

            // Striations: fibres running along the sweep (fast in u, slow in v),
            // two scales so no single rhythm is legible. This is the whole reason
            // the mask is a texture and not a formula in the vertex colour — per
            // vertex there are 24 samples along the arc and 2 across, which
            // cannot hold detail at this frequency.
            float lines  = 0.72f + 0.28f * sinf(u * PI * 9.0f  + v * 1.7f);
            float lines2 = 0.85f + 0.15f * sinf(u * PI * 23.0f - v * 0.6f);
            // ...and a slow swell ALONG the sweep. Without it the band is a
            // uniform sheet of light, which is what makes a clean arc read as
            // painted glass rather than as something violent that just happened.
            // Kept shallow and low-frequency: any deeper and it reads as a dash.
            float pulse  = 0.80f + 0.20f * sinf(v * PI * 5.0f + u * 2.3f);

            float a = SweepSlash_Profile(u, true) * lines * lines2 * pulse;
            a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            ImageDrawPixel(&img, x, y, (Color){255, 255, 255,
                                               (unsigned char)(a * 255.0f)});

            // The halo keeps the lengthwise swell (it is the same event) but not
            // the cross-blade fibres, which at 1.8x width would just double the
            // striation at a different scale and read as moire.
            float h = SweepSlash_Profile(u, false) * pulse;
            h = h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
            ImageDrawPixel(&halo, x, y, (Color){255, 255, 255,
                                                (unsigned char)(h * 255.0f)});
        }
    }
    s_slashTex     = LoadTextureFromImage(img);
    s_slashHaloTex = LoadTextureFromImage(halo);
    UnloadImage(img);
    UnloadImage(halo);
    if (s_slashHaloTex.id != 0)
    {
        SetTextureFilter(s_slashHaloTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_slashHaloTex, TEXTURE_WRAP_CLAMP);
    }
    if (s_slashTex.id != 0)
    {
        SetTextureFilter(s_slashTex, TEXTURE_FILTER_BILINEAR);
        // CLAMP, not REPEAT: the arc-length UV spans exactly [0,1] and bilinear
        // sampling at the very ends would otherwise fetch across the wrap and
        // paint a sliver of the opposite end onto the tip.
        SetTextureWrap(s_slashTex, TEXTURE_WRAP_CLAMP);
    }
}

static void SweepSlash_InitShared(void)
{
    if (s_slashInit) return;

    // Bright while the head is out in front, then it goes. The band is already
    // shortening on its own by then, so this curve only has to take away what
    // is left — a fade that starts early would kill the sweep at its clearest.
    FloatCurve_AddStop(&s_slashFade, 0.00f, 0.90f);
    FloatCurve_AddStop(&s_slashFade, 0.08f, 1.00f);
    FloatCurve_AddStop(&s_slashFade, 0.50f, 0.90f);
    FloatCurve_AddStop(&s_slashFade, 0.85f, 0.35f);
    FloatCurve_AddStop(&s_slashFade, 1.00f, 0.00f);

    SweepSlash_BuildMask();

    Tuning_RegisterFloat("slash_width",   &s_slashWidth,   1.0f);
    Tuning_RegisterFloat("slash_tilt",    &s_slashTilt,    0.45f);
    Tuning_RegisterFloat("slash_distort", &s_slashDistort, 1.0f);
    Tuning_RegisterFloat("slash_sparks",  &s_slashSparks,  1.0f);

    s_slashInit = true;
}

// Half-width envelope from tail (s=0) to head (s=1). A LENS: pointed at both
// ends, widest in the middle.
//
// The first version was `powf(s, 0.5f) * (1 - smoothstep((s-0.86)/0.14))` — zero
// at both ends on paper, and wrong on screen. `sqrt` reaches half width by
// s = 0.25, so the band was already fat a quarter of the way from the tail and
// only pinched in the last few percent, while the head's taper was squeezed into
// 14%. What that draws is a blunt club leading a long thin streamer — the owner
// read it immediately as "đầu bự đi trước, đầu nhỏ theo sau", which is a comet,
// not a cut. A blade leaves a shape that is pointed at BOTH ends because both
// ends are the same edge at different times.
//
// `sin(PI*s)` is that lens; the exponent under 1 flares it away from each tip
// fast, so the points stay needle-sharp instead of reading as a fat leaf.
static float SweepSlash_WidthEnv(float s)
{
    if (s <= 0.0f || s >= 1.0f) return 0.0f;
    return powf(sinf(PI * s), 0.85f);
}

// Continuous: call every frame with `t01` running 0 → 1 over the swing.
// `origin` = the pivot (shoulder/hilt hand), `dir` = the direction the arc's
// MIDPOINT points, `length` = arc radius in metres, `arcRad` = total swept angle
// in radians (2.2 ~ 126 degrees, a full body swing).
void VFX_ComposeSweepSlash(Vector3 origin, Vector3 dir, VC_MaterialId mat,
                           float length, float arcRad, float t01)
{
    SweepSlash_InitShared();
    if (length <= 0.0f) length = 1.5f;
    if (arcRad <= 0.0f) arcRad = 2.2f;
    if (arcRad > 2.0f * PI) arcRad = 2.0f * PI;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;

    float fade = SkillCurve_Eval(&s_slashFade, t01);
    if (fade <= 0.001f) return;

    // ── The swing plane ─────────────────────────────────────────────────────
    // `dir` fixes where the arc points; the plane it sweeps in is tilted off
    // horizontal by `slash_tilt` so the cut reads as diagonal. A flat sweep
    // (tilt 0) is a disc on the ground and looks like a shockwave, which is a
    // different effect.
    Vector3 d = Vector3Normalize(dir);
    if (Vector3LengthSqr(d) < 0.5f) d = (Vector3){0.0f, 0.0f, 1.0f};
    Vector3 up   = {0.0f, 1.0f, 0.0f};
    Vector3 side = Vector3CrossProduct(up, d);
    if (Vector3LengthSqr(side) < 1e-6f) side = (Vector3){1.0f, 0.0f, 0.0f};
    side = Vector3Normalize(side);

    Vector3 n = Vector3Add(Vector3Scale(up,   cosf(s_slashTilt)),
                           Vector3Scale(side, -sinf(s_slashTilt)));
    // Orthogonalise against `d` — `up` is only perpendicular to `d` while `d` is
    // horizontal, and a caller aiming up a slope would otherwise get a plane the
    // arc does not lie in (the ribbon's fixed normal would then be wrong too).
    n = Vector3Normalize(Vector3Subtract(n, Vector3Scale(d, Vector3DotProduct(n, d))));
    Vector3 axU = d;                                             // arc midpoint
    Vector3 axV = Vector3Normalize(Vector3CrossProduct(n, axU)); // sweep direction

    // ── Head and tail ───────────────────────────────────────────────────────
    // Two angles walking the same arc on different clocks. The head is
    // ease-OUT — a blade is fastest at the start of its follow-through — and the
    // tail is smoothstep, so it leaves late and arrives gently.
    const float half = arcRad * 0.5f;
    float headT = Clamp(t01 / SLASH_SWEEP_T, 0.0f, 1.0f);
    float tailT = Clamp((t01 - SLASH_TAIL_LAG) / (SLASH_TAIL_END - SLASH_TAIL_LAG),
                        0.0f, 1.0f);
    float headE = 1.0f - powf(1.0f - headT, 2.2f);
    float tailE = SmoothStep01(tailT);
    float aHead = -half + arcRad * headE;
    float aTail = -half + arcRad * tailE;
    float span  = aHead - aTail;
    if (span <= 0.004f) return;

    // As the tail closes the band would end up short AND fat, which reads as a
    // blob. Width collapses with it once the band is under a third of the arc.
    float shrink = Clamp(span / (0.33f * arcRad), 0.0f, 1.0f);
    // WIDTH IS KEYED TO THE ARC'S LENGTH, NOT ITS RADIUS. What makes a band read
    // as a cut is its ASPECT RATIO — a blade trail is roughly 1:20 against the
    // distance it travelled. Keyed to radius (0.105 * length) the ratio moved
    // with `arcRad`: at the bench's 2.2 rad the head travels 4 m and the band was
    // 0.38 m wide, i.e. 1:10, which draws a LEAF. Same number at a 0.6 rad flick
    // would have been 1:3, a paddle.
    //   arcLen = length * arcRad   (metres the head actually travels)
    //   full width = 2 * 0.022 * arcLen  ~ 1:23
    float arcLen = length * Clamp(arcRad, 0.5f, 3.2f);
    float halfW  = arcLen * 0.022f * s_slashWidth * shrink;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Color bodyCol = m->body;
    Color glowCol = m->glow;

    // ── The band ────────────────────────────────────────────────────────────
    // Additive, depth-test on / depth-write off, batch flushed either side —
    // ENGINE_LANDMINES §1: an unflushed depth-state change leaks into whatever
    // the batch was already holding.
    VFXRenderScope renderScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);

    static RibbonPoint pts[SWEEP_SLASH_POINTS];

    // Three passes, same reason as VFX_ComposeRuneCircle: a ribbon goes through
    // the immediate-mode path, so it has no emissiveBoost — its vertex colour is
    // 8-bit and caps at 1.0, under the bloom threshold. Drawing it again is the
    // only way plain geometry gets past 1.0 in the HDR buffer.
    // The halo was 2.7x and it is what made the band read as fog. It exists to
    // put light BEHIND the blade, not to be the blade's silhouette.
    static const float passW[3]     = {1.5f, 1.0f, 0.34f};  // x on half-width
    static const float passA[3]     = {0.18f, 1.0f, 1.0f};  // x on alpha
    static const float passWhite[3] = {0.0f, 0.35f, 0.95f}; // toward white

    for (int pass = 0; pass < 3; pass++)
    {
        for (int i = 0; i < SWEEP_SLASH_POINTS; i++)
        {
            float s   = (float)i / (float)(SWEEP_SLASH_POINTS - 1); // 0 tail → 1 head
            float ang = aTail + span * s;
            // The head reaches marginally further than the tail: a real swing
            // extends as the arm straightens, and it stops the arc from reading
            // as a perfect compass circle.
            float rr  = length * (0.94f + 0.06f * s);

            // PASSES ARE ALIGNED BY THEIR OUTER EDGE, NOT BY THEIR CENTRE.
            // The mask puts its hot rim at a fixed FRACTION of the strip's width
            // (u ~ 0.05), so a strip centred on the same path but 1.8x as wide
            // lands its rim 1.8x further out — three passes drew three rims at
            // three radii and the slash came out as parallel wires. Shifting each
            // pass's centre inward by its own extra half-width makes every rim
            // fall on the same world circle: one edge, with the wider passes
            // reaching further INWARD as glow behind it.
            float env = SweepSlash_WidthEnv(s);
            float hw  = halfW * passW[pass] * env;
            rr += halfW * env * (1.0f - passW[pass]);

            Vector3 p = Vector3Add(origin,
                          Vector3Add(Vector3Scale(axU, cosf(ang) * rr),
                                     Vector3Scale(axV, sinf(ang) * rr)));
            pts[i].position  = p;
            pts[i].halfWidth = hw;

            // Brightness rides toward the head — the tail is what is left of the
            // light, not a second edge.
            float a = fade * passA[pass] * Math_Mix(0.25f, 1.0f, powf(s, 1.4f));
            // Hue also travels: the element's body colour in the cooling tail,
            // its glow colour at the head. A single flat colour along the band
            // is what makes an additive strip read as painted plastic.
            float hot = SmoothStep01((s - 0.45f) / 0.55f);
            Color c = VC_MixColor(bodyCol, glowCol, hot);
            c = VC_Whiten(c, passWhite[pass]);
            pts[i].tint = ColorAlpha(c, Clamp(a, 0.0f, 1.0f));
        }

        // Arc-length UV, per the spec: the band's length changes every frame, so
        // an index/count `v` would slide the striations along the blade.
        Ribbon_ComputeArcLengthUV(pts, SWEEP_SLASH_POINTS);
        // The halo gets the rim-LESS sheet: it shares the outer edge with the
        // other two, so its own rim would land on the same line 1.8x thicker and
        // turn the edge back into a gradient.
        Texture2D tex = (pass == 0 && s_slashHaloTex.id != 0) ? s_slashHaloTex
                                                              : s_slashTex;
        DrawRibbonStripEx(pts, SWEEP_SLASH_POINTS, tex, camera,
                          RIBBON_FIXED_NORMAL, n);
    }

    VFXRender_EndDraw(&renderScope);

    // ── Refraction behind the edge ──────────────────────────────────────────
    // A RATE, not one per call: this function runs every frame, and a source per
    // frame would be 60/s (the pool holds 16) and would change density with the
    // frame rate. 9/s against a 0.16 s life is ~1.5 live at any moment.
    // The distortion pass costs per-fragment across the whole screen for every
    // active source (PROGRESS, 28/07/2026) — hence `slash_distort` as a switch,
    // and hence the deliberately short life.
    float dt = TimeFX_RawDelta();
    // E8 tier budget. The distortion pass costs per fragment across the WHOLE
    // screen for every live source, which is exactly the shape of cost the Mali
    // A33 cannot absorb. Below MED the slash keeps its geometry and loses only
    // the refraction — the effect still reads, it just stops bending the world.
    bool allowDistort = (GfxQuality_Get() >= GFX_MED);
    if (allowDistort && s_slashDistort > 0.5f && headT < 1.0f)
    {
        s_slashDistortAcc += dt * SLASH_DISTORT_RATE;
        while (s_slashDistortAcc >= 1.0f)
        {
            s_slashDistortAcc -= 1.0f;
            float ang = aHead - span * 0.12f;
            Vector3 p = Vector3Add(origin,
                          Vector3Add(Vector3Scale(axU, cosf(ang) * length),
                                     Vector3Scale(axV, sinf(ang) * length)));
            ScreenDistort_Add(p, length * 0.55f, 0.16f, 0.16f, 2.2f);
        }
    }

    // ── Sparks off the leading edge ─────────────────────────────────────────
    // Also a rate. Small, hard-stretched, additive, and very short-lived: they
    // are the grit the edge throws, not a second effect. ~24/s x 0.14 s ~ 3 live.
    if (s_slashSparks > 0.01f && headT < 1.0f)
    {
        s_slashSparkAcc += dt * SLASH_SPARK_RATE * s_slashSparks;
        int guard = 0;
        while (s_slashSparkAcc >= 1.0f && guard++ < 8)
        {
            s_slashSparkAcc -= 1.0f;
            // Born at the TIP, not along the band. Spread over the leading third
            // they were laid down like beads on a wire — the arc got decorated
            // instead of the edge shedding.
            float sp  = Math_Mix(0.92f, 1.0f, Random01());
            float ang = aTail + span * sp;
            Vector3 radial  = Vector3Add(Vector3Scale(axU, cosf(ang)),
                                         Vector3Scale(axV, sinf(ang)));
            Vector3 tangent = Vector3Subtract(Vector3Scale(axV, cosf(ang)),
                                              Vector3Scale(axU, sinf(ang)));
            float rr = length * Math_Mix(0.94f, 1.06f, Random01());
            Vector3 p = Vector3Add(origin, Vector3Scale(radial, rr));

            // Thrown forward along the sweep, with a little scatter outward —
            // the blade's own motion is what carries them.
            Vector3 vel = Vector3Add(Vector3Scale(tangent, Math_Mix(2.5f, 5.0f, Random01())),
                                     Vector3Scale(radial,  Math_Mix(0.2f, 1.4f, Random01())));
            vel.y += (Random01() - 0.5f) * 0.8f;

            SpawnParticle((ParticleConfig){
                .position = p,
                .velocity = vel,
                .radius   = Math_Mix(0.009f, 0.020f, Random01()),
                // Short enough that a spark is GONE before the head has moved a
                // hand's width past it. Anything longer and the sparks stay put
                // while the blade leaves, which is the bead-chain.
                .lifetime = Math_Mix(0.09f, 0.19f, Random01()),
                .colorStart = VC_WithAlpha(VC_Whiten(glowCol, 0.45f),
                                           (unsigned char)(140.0f * fade)),
                .colorEnd   = VC_WithAlpha(bodyCol, 0),
                .render.blendMode = VFX_BLEND_ADDITIVE,
                .render.unlit = 1,
                // 1.5 put every spark over the bloom threshold, and bloom turns
                // a 2 cm sprite into a 10 cm bead. They should catch the light,
                // not BE the light.
                .render.emissiveBoost = 1.15f,
                // stretchFactor = 1 + speed*strength (particle_system.c:1003).
                // At ~4 m/s, 0.10 gave 1.4x — a round dot. 1.1 gives ~5x, which
                // is a streak.
                .stretchStrength = 1.10f,
                .stretchMinSpeed = 0.5f,
            });
        }
    }
}
