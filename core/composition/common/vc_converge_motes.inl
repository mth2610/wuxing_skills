// ── P2. VFX_ComposeConvergeMotes — qi drawn in from the space around ─────────
//
// EXTRACTED FROM `VFX_ComposeChargeConverge` on 30/07/2026, so that a summon, a
// drain and an absorb could have the indraught without the hot core the charge
// wanted at the middle of it (VFX_PLAN §4.1).
//
// ── REBUILT 28/08/2026, SECOND PASS: PARTICLE RIBBONS → SWEPT TRAILS ────────
//
// Owner's verdict on the first pass was blunt — the effect was many thin threads
// and it looked cheap. The direction with it names the primitive: *"trail có thể
// dùng backdrop trail, nhưng thưa hơn, làm nó sáng và bloom"*. Three decisions,
// and none of them is a tuning value:
//
//   1. THE RIBBON IS A REAL TRAIL, NOT A PARTICLE'S TAIL. A particle ribbon is
//      8 history points with no cloth, no flow, no layer stack and no HDR lift
//      — a polyline that cannot be lit. `TRAIL_PRESET_BACKDROP` is the widest
//      swept recipe there is (half-width : travelled length = 1:10, four times
//      MAIN's) with the sheet, the flow scroll and the sag of the trail system
//      behind it.
//   2. THEREFORE FEW, NOT MANY. A swept trail is a simulated entity out of a
//      pool of eight, so this can no longer spend threads by the dozen — and it
//      should not: sparse is what the owner asked for, and a broad ribbon needs
//      space around it to read as broad. `charge_streams` (4) is the whole
//      population.
//   3. BRIGHT, BY LIFTING THE INSTANCE. BACKDROP is authored as a DIM underlay
//      to sit behind another trail, so on its own it is grey. `hdrGain` is the
//      field the bright pass measures, every trail owns a copy of its recipe,
//      and `VFX_TrailSetHdrGain` lifts THIS instance — raising the shared row
//      would have dragged the fixtures that measure BACKDROP's darkening along
//      with it.
//
// WHAT THIS FILE NOW OWNS, and what it stopped owning. It integrates its own
// streamers — position and velocity per slot, one attractor and one drag — and
// hands each one's Matrix to a trail. The particle system, the mesh emitter, the
// shared ForceField pool and the per-thread colour curves are all gone: a trail
// follows a transform, so the motion has to be in a transform this file writes.
// That also makes it STATEFUL, hence the `VC_ConvergeMotes_Update/_Draw3D` pair
// that declares an archetype to scripts/sync_vfx_test.py.
//
// THE MOTION IS UNCHANGED, and it is the part that survived both rewrites:
// a streamer appears at a random point in the VOLUME around the centre (not on a
// shell — "sợi trail xuất hiện ngẫu nhiên trong không gian"), is launched
// SIDEWAYS at about half the circular-orbit speed of the attractor, and is drawn
// in along a curve. Below orbital speed it cannot hold station and must fall;
// with a real sideways component it falls along an arc instead of down a spoke.
// Every duration is a fraction of the orbital PERIOD, never a number of seconds,
// so the tell reads the same at 0.3 m and at 3 m (core/docs/LANDMINES.md).

#define CONVERGE_MAX_STREAMS 6

typedef struct {
    bool    active;
    int     trail;      // handle into the swept-trail pool, -1 if it failed
    Matrix  xf;         // what the trail follows — caller-owned, so it lives here
    Vector3 pos, vel;
    Vector3 center;     // the converge it belongs to, captured at birth
    float   pull;       // attractor strength, likewise
    float   drag;
    float   ball;       // metres: the ball's surface — where the taper starts
    float   sink;       // metres: arrive inside this and the ribbon is gone
    float   age, life;
} ConvergeStream;

static ConvergeStream s_convStreams[CONVERGE_MAX_STREAMS];
// The wave clock. Streamers are not launched one at a time any more — see
// ConvergeMotes_LaunchWave.
static float s_convWaveGap = 0.0f;
static bool  s_chargeInit = false;

// Live knobs — a cast tell is judged by eye (core/CLAUDE.md §5). The KEYS of the
// four that existed before are unchanged on purpose: a tuning.cfg written
// against the old converge must keep meaning what it meant.
static float s_chargeRate  = 1.0f;   // x on how often a streamer is launched
static float s_chargeSize  = 1.0f;   // x on ribbon width
static float s_chargePull  = 1.0f;   // x on the attractor (how tight the curve)
static float s_chargeSwirl = 1.0f;   // x on the sideways launch speed
// How many streamers may be alive at once. THE dial for "thưa hơn", and it is
// also a budget: the swept-trail pool is eight for the whole engine.
static float s_chargeStreams = 4.0f;
// The emission lift handed to each ribbon. BACKDROP ships at 1.0 (a dim
// underlay); bloom's bright pass is what this is aimed at.
static float s_chargeGlow = 4.0f;
// THE SINK: the radius, as a fraction of `radius`, at which a streamer counts as
// ARRIVED and stops feeding its ribbon.
//
// It lives here rather than in the score, even though the score is what draws
// the ball, because the two numbers must be ONE number: a sink smaller than the
// ball hides the last of every ribbon inside it, and a sink larger leaves the
// ribbons stopping short in mid-air with a gap between them and the thing they
// are supposed to be pouring into. The score reads it back through
// VC_ConvergeMotesSinkFrac(). `charge_ball` keeps the key it was registered
// under when the score owned it.
static float s_chargeBall = 0.26f;

// `charge_size` also scales the ball in `VFX_ComposeChargeConverge`, which is a
// different file, so the dial has to be readable from there.
//
// NOT static, and not read directly either, and both of those are deliberate:
//   - A direct read of `s_chargeSize` from the composite would COMPILE — every
//     .inl is pasted into one translation unit — and would silently depend on
//     include ORDER, which scripts/sync_vfx_test.py owns and which appends new
//     files at the end.
//   - A `static` forward declaration in the composite would fix the order
//     problem and trip core/tests/composition_tu_test.c, which flags any
//     file-scope `static` name appearing in two files in the TU.
// So it is an ordinary function with its prototype in visual_composer.h.
float VC_ConvergeMotesSizeMul(void)
{
    return s_chargeSize;
}

// The sink radius as a fraction of the converge radius — the score sizes its
// ball with this so the ribbons end exactly where the ball's surface is.
float VC_ConvergeMotesSinkFrac(void)
{
    return s_chargeBall;
}

static void ConvergeMotes_InitShared(void)
{
    if (s_chargeInit) return;
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (docs/LANDMINES.md).
    Tuning_RegisterFloat("charge_rate",    &s_chargeRate,    1.0f);
    Tuning_RegisterFloat("charge_size",    &s_chargeSize,    1.0f);
    Tuning_RegisterFloat("charge_pull",    &s_chargePull,    1.0f);
    Tuning_RegisterFloat("charge_swirl",   &s_chargeSwirl,   1.0f);
    Tuning_RegisterFloat("charge_streams", &s_chargeStreams, 4.0f);
    Tuning_RegisterFloat("charge_glow",    &s_chargeGlow,    4.0f);
    Tuning_RegisterFloat("charge_ball",    &s_chargeBall,    0.26f);
    for (int i = 0; i < CONVERGE_MAX_STREAMS; i++) s_convStreams[i].trail = -1;
    s_chargeInit = true;
}

// The attractor's strength, in ONE place: the launch code solves for orbital
// speed with the same number the integrator accelerates by, and two copies of it
// would drift the moment either was tuned.
static float ConvergeMotes_Pull(float radius, float t01)
{
    return radius * Math_Mix(9.0f, 18.0f, t01) * s_chargePull;
}

// WHERE THE STREAMERS COME FROM: not a surface, a VOLUME — some near, some three
// radii out. A single launch shell puts every ribbon the same distance away and
// the whole population arrives together, which reads as a mechanism rather than
// as loose qi being gathered. The outer bound closes in as the charge fills.
static float ConvergeMotes_CloudInner(float radius) { return radius * 1.60f; }
static float ConvergeMotes_CloudOuter(float radius, float t01)
{
    return radius * Math_Mix(3.4f, 2.4f, t01);
}

// HOW LONG ONE TURN TAKES on a circular orbit of radius `orbitR` — the clock
// every duration here is quoted against. The engine's point attractor is
// a = strength/(dist + 1) (force_field.c:244), so v = sqrt(a*r), T = 2*PI*r/v.
static float ConvergeMotes_Period(float pull, float orbitR)
{
    if (orbitR < 1e-4f) orbitR = 1e-4f;
    float accel = pull / (orbitR + 1.0f);
    float v     = sqrtf(accel * orbitR);
    if (v < 1e-4f) v = 1e-4f;
    return (2.0f * PI * orbitR) / v;
}

// A launch point on the unit sphere, AREA-uniform: cos(theta) is what has to be
// uniform, not theta. Sampling theta directly (what VC_DirCone does at a PI cone)
// piles launch points onto the two poles.
static Vector3 ConvergeMotes_SphereDir(float u1, float u2)
{
    float z   = 1.0f - 2.0f * u1;
    float r   = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    float phi = u2 * 2.0f * PI;
    return (Vector3){ r * cosf(phi), z, r * sinf(phi) };
}

// `absorbed` = the head reached the middle of the ball. That ribbon is CUT, not
// wound down: it was taken in, and a strip left drifting and fading in open space
// afterwards is the drifting this effect is not supposed to do. A streamer that
// ends any other way (the fallback timer) still gets the gentle wind-down, since
// there is nothing at that point to justify a cut.
static void ConvergeMotes_Release(ConvergeStream *st, bool absorbed)
{
    if (st->trail >= 0)
    {
        if (absorbed) VFX_Trail_Extinguish(st->trail);
        else          VFX_Trail_Stop(st->trail);
    }
    st->trail  = -1;
    st->active = false;
}

// Integrating the streamers is this file's job now, because a trail follows a
// TRANSFORM: there is no particle for a ForceField to act on.
static void VC_ConvergeMotes_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < CONVERGE_MAX_STREAMS; i++)
    {
        ConvergeStream *st = &s_convStreams[i];
        if (!st->active) continue;

        Vector3 toC = Vector3Subtract(st->center, st->pos);
        float   d   = Vector3Length(toC);
        if (d > 1e-4f)
        {
            // The same law the launch speed was solved against, integrated
            // explicitly: a = pull/(d+1) toward the centre, minus drag.
            float a = st->pull / (d + 1.0f);
            st->vel = Vector3Add(st->vel, Vector3Scale(toC, (a / d) * dt));
        }
        st->vel = Vector3Scale(st->vel, 1.0f - fminf(0.9f, st->drag * dt));

        // CAPTURE. Inside one and a half ball radii the velocity is steered
        // toward the centre, hard and increasingly so. Without it a streamer
        // keeps the sideways speed it was launched with and swings around the
        // ball instead of entering it: at the smallest sizes NOT ONE of them
        // reached the middle (the arrival mirror in
        // core/tests/converge_motes_test.c measured 0%). The alternative — a
        // launch aimed straight enough to fall to the centre unaided — is the
        // spoke pattern the owner rejected, so the curve is kept out here and
        // the last stretch is where the ball takes over.
        if (d < st->ball * 1.5f && d > 1e-4f)
        {
            float k     = 1.0f - d / (st->ball * 1.5f);   // 0 at the edge, 1 at the middle
            float steer = 6.0f * k * dt;
            if (steer > 1.0f) steer = 1.0f;
            float speed = Vector3Length(st->vel);
            Vector3 radial = Vector3Scale(toC, speed / d);
            st->vel = Vector3Add(st->vel,
                                 Vector3Scale(Vector3Subtract(radial, st->vel), steer));
        }
        st->pos = Vector3Add(st->pos, Vector3Scale(st->vel, dt));
        st->xf  = MatrixTranslate(st->pos.x, st->pos.y, st->pos.z);

        // WHAT MAKES IT READ AS BEING ABSORBED, rather than as a ribbon that
        // happened to expire nearby. Two things, and the first is the one that
        // was missing: a streamer ends when it ARRIVES — at the ball's surface,
        // by distance — not when a timer says so. The timer is only the fallback
        // for a path that somehow never gets there.
        //
        // The second is the taper: from a little outside the ball the ribbon's
        // width is ramped to nothing, so it narrows into the surface instead of
        // stopping at full width. VFX_TrailSetWidth ramps rather than assigns,
        // which is exactly the wind-down this wants.
        // THE TAPER RUNS INSIDE THE BALL: full width at the surface, nothing at
        // the middle. That is what lets the cut below be instant without popping
        // — by the time the head reaches the centre the ribbon is already a
        // thread. It must not start any further out than the surface, because
        // VFX_TrailSetWidth scales the WHOLE strip, so an early taper thins the
        // ribbon while it is still crossing open space and reads as fading before
        // it arrives.
        if (d < st->ball && st->trail >= 0)
        {
            float t = (d - st->sink) / (st->ball - st->sink);  // 1 at the surface, 0 at the middle
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            VFX_TrailSetWidth(st->trail, t);
        }

        st->age += dt;
        if (d <= st->sink)            ConvergeMotes_Release(st, true);
        else if (st->age >= st->life) ConvergeMotes_Release(st, false);
    }
}

// Nothing to draw: the trails draw themselves through the trail system. The pair
// with Update is what declares this file stateful to the archetype scan, and the
// empty half is the price of that declaration being mechanical.
static void VC_ConvergeMotes_Draw3D(Camera3D cam)
{
    (void)cam;
}

// ONE streamer, launched along a GIVEN direction. The direction is the wave's
// decision, not this function's — see ConvergeMotes_LaunchWave.
static void ConvergeMotes_LaunchAt(Vector3 n, float orbitR, float tanFrac, float inFrac,
                                   Vector3 center, VC_MaterialId mat,
                                   float radius, float t01)
{
    int slot = -1;
    for (int i = 0; i < CONVERGE_MAX_STREAMS; i++)
        if (!s_convStreams[i].active) { slot = i; break; }
    if (slot < 0) return;

    float pull = ConvergeMotes_Pull(radius, t01);
    ConvergeStream *st = &s_convStreams[slot];

    // WHERE IT APPEARS along that direction. The distance and the launch mix
    // come from the WAVE, jittered per streamer — see ConvergeMotes_LaunchWave
    // for why they cannot be drawn independently here.
    st->pos = Vector3Add(center, Vector3Scale(n, orbitR));

    // WHICH WAY IT CURLS: one common handedness — every ribbon bends the same
    // way, which is what makes four of them read as ONE indraught — tilted per
    // streamer so the paths fan out instead of stacking into a single disc.
    Vector3 tilt = ConvergeMotes_SphereDir(Random01(), Random01());
    Vector3 axis = Vector3Normalize(Vector3Add((Vector3){ 0.0f, 1.0f, 0.0f },
                                               Vector3Scale(tilt, 0.55f)));
    Vector3 tang = Vector3CrossProduct(axis, n);
    float   tlen = Vector3Length(tang);
    if (tlen < 1e-3f)   // born on the axis: any perpendicular will do
    {
        tang = Vector3CrossProduct(n, (Vector3){ 1.0f, 0.0f, 0.0f });
        tlen = Vector3Length(tang);
        if (tlen < 1e-3f) { tang = (Vector3){ 0.0f, 0.0f, 1.0f }; tlen = 1.0f; }
    }
    tang = Vector3Scale(tang, 1.0f / tlen);

    // HOW FAST: measured against the CIRCULAR orbit speed at its own distance.
    // A FRACTION of it sideways — sub-orbital, so it cannot hold station and
    // must fall — plus an inward part, so it falls along a curve. All tangent
    // and it circles forever; all inward and the population is a wheel of
    // spokes.
    float   accel  = pull / (orbitR + 1.0f);
    float   vOrbit = sqrtf(accel * orbitR);
    Vector3 vel    = Vector3Scale(tang, vOrbit * tanFrac * s_chargeSwirl);
    vel = Vector3Add(vel, Vector3Scale(n, -vOrbit * inFrac));
    st->vel = vel;

    float period = ConvergeMotes_Period(pull, orbitR);
    st->center = center;
    st->pull   = pull;
    st->drag   = 1.6f / period;     // per TURN, not per second — see the header
    st->ball   = radius * s_chargeBall * s_chargeSize;
    // THE STREAMER GOES TO THE MIDDLE, not to the surface. Owner: "nó xuất hiện
    // rồi di chuyển vào tâm quả cầu rồi biến mất luôn". Ending at the surface
    // leaves the ribbon stopping on the outside of the ball, which reads as
    // hitting it rather than as being taken into it.
    st->sink   = st->ball * 0.25f;
    st->age    = 0.0f;
    // LONG ENOUGH TO ARRIVE, and this number is measured rather than felt. The
    // timer is a FALLBACK — a streamer is supposed to end by reaching the ball —
    // so if it fires first the ribbon vanishes in mid-air short of the
    // destination, which is exactly what the owner saw ("có những trail nó ko di
    // chuyển tới tâm quả cầu đã bắt đầu biến mất"). Integrating the real launch
    // spread against the real attractor: at 0.42-0.60 of a turn only 4-35% of
    // streamers ever reached the sink; at 1.2-1.6 it is 95-100% at every size
    // from 0.3 m to 3 m, with the slowest arriving at 1.45 turns.
    st->life   = period * Math_Mix(1.2f, 1.7f, Random01());
    st->xf     = MatrixTranslate(st->pos.x, st->pos.y, st->pos.z);
    st->active = true;

    // The ribbon. Width is a fraction of the converge, and BACKDROP caps it
    // against the length the head has actually travelled (1:10), so a freshly
    // launched streamer opens out instead of popping to full width. `lifetime`
    // is the ribbon's own memory — most of the flight, so the arc is a long
    // sweep rather than a stub.
    st->trail = VFX_ComposeTrail(&st->xf, mat,
                                 radius * Math_Mix(0.10f, 0.17f, Random01()) * s_chargeSize,
                                 period * 0.55f, TRAIL_PRESET_BACKDROP);
    if (st->trail >= 0)
        VFX_TrailSetHdrGain(st->trail, s_chargeGlow * Math_Mix(0.8f, 1.25f, t01));
}


// ── THE WAVE ────────────────────────────────────────────────────────────────
//
// Owner, 28/08/2026: *"kiểu bay lượn lờ của các sợi trail ko hợp lý, nó nên xuất
// hiện đồng loạt rồi bị hút vào tâm"*. Launching one streamer at a time — which
// is what a rate-driven emitter does — gives four ribbons at four unrelated
// stages of four unrelated flights, and that reads as drifting, not as a cast.
// A charge tell has a BEAT: everything appears at once, everything is pulled in,
// a breath, and again.
//
// So the whole population goes out together, on directions spread evenly over
// the sphere rather than sampled independently — with four ribbons, independent
// samples clump often enough that "from every side" never actually happens. The
// spread is a Fibonacci lattice (golden-angle in longitude, uniform in cos), and
// it is ROTATED per wave: a fixed lattice would put the first ribbon of every
// wave in the same place and the beat would read as a rotating machine.
static void ConvergeMotes_LaunchWave(Vector3 center, VC_MaterialId mat,
                                     float radius, float t01)
{
    int cap = (int)(s_chargeStreams + 0.5f);
    if (cap < 1) cap = 1;
    if (cap > CONVERGE_MAX_STREAMS) cap = CONVERGE_MAX_STREAMS;

    // ONE DISTANCE AND ONE LAUNCH MIX FOR THE WHOLE WAVE, jittered only slightly
    // per streamer. This is what actually makes the beat: flight time is set by
    // the birth radius and the launch speed, so drawing those independently — as
    // the rate-driven version did — spreads four arrivals over more than a turn
    // and the wave dissolves back into the drifting it was meant to replace.
    // Each wave still picks its own values, so no two beats are identical.
    float waveR   = Math_Mix(ConvergeMotes_CloudInner(radius),
                             ConvergeMotes_CloudOuter(radius, t01), Random01());
    float waveTan = Math_Mix(0.34f, 0.52f, Random01());
    float waveIn  = Math_Mix(0.44f, 0.66f, Random01());

    const float golden = 2.39996323f;      // radians; the golden angle
    float phi0 = Random01() * 2.0f * PI;   // this wave's rotation...
    float zoff = Random01();               // ...and its offset along the axis
    for (int i = 0; i < cap; i++)
    {
        float z   = 1.0f - 2.0f * (((float)i + zoff) / (float)cap);
        float r   = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        float phi = phi0 + golden * (float)i;
        Vector3 n = { r * cosf(phi), z, r * sinf(phi) };
        ConvergeMotes_LaunchAt(n,
                               waveR   * Math_Mix(0.94f, 1.06f, Random01()),
                               waveTan * Math_Mix(0.92f, 1.08f, Random01()),
                               waveIn  * Math_Mix(0.92f, 1.08f, Random01()),
                               center, mat, radius, t01);
    }

    // The breath between waves, as a fraction of the reference orbit so it keeps
    // its shape at every scale. It SHORTENS as the charge fills, which is what
    // makes a wind-up feel like it is winding up rather than repeating.
    float midCloud  = 0.5f * (ConvergeMotes_CloudInner(radius) +
                              ConvergeMotes_CloudOuter(radius, t01));
    float refPeriod = ConvergeMotes_Period(ConvergeMotes_Pull(radius, t01), midCloud);
    s_convWaveGap = refPeriod * Math_Mix(0.30f, 0.10f, t01);
}

// Continuous: call once per frame while the converge should exist.
// `radius`    = the scale of the tell in metres — streamers appear between 1.15
//               and 3 of it out.
// `t01`       = progress 0→1: launch rate, pull, brightness.
// `moteCount` = how eagerly streamers are launched, in launches per second. It is
//               NOT how many exist: `charge_streams` caps the live population,
//               because each one is a simulated trail out of a pool of eight.
void VFX_ComposeConvergeMotes(Vector3 center, VC_MaterialId mat, float radius,
                              float t01, int moteCount)
{
    ConvergeMotes_InitShared();
    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;
    if (moteCount < 1) moteCount = 1;

    // THE BEAT. A wave goes out when the last one has been absorbed and the
    // breath after it has elapsed — self-clocking against the flight time, so it
    // stays a beat at every scale and never overlaps itself into a drizzle.
    //
    // `moteCount` scales the beat rather than a spawn rate now: more of it means
    // a shorter breath, i.e. waves that follow each other more urgently. The
    // POPULATION is charge_streams, and the fixture's 5 is the neutral value.
    int live = 0;
    for (int i = 0; i < CONVERGE_MAX_STREAMS; i++)
        if (s_convStreams[i].active) live++;

    if (s_convWaveGap > 0.0f)
        s_convWaveGap -= TimeFX_RawDelta() * (0.2f * (float)moteCount) * s_chargeRate;

    if (live == 0 && s_convWaveGap <= 0.0f)
        ConvergeMotes_LaunchWave(center, mat, radius, t01);
}
