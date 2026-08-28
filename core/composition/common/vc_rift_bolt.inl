// RIFT BOLT — the head of a projectile, rebuilt from the other side.
//
// `VFX_ComposeProjectile` was deleted on 17/08/2026 with deliberately no
// successor, and the deletion note in visual_composer.h says why: measured
// against the bright-background chart it was the worst of the three largest
// in-band effects on every axis — 79% of its body area gone on white, only
// 28.7% of its own footprint attenuated, internal structure collapsing 10x.
// It was visible because it ADDED light, which is a property of the night
// arena it was tuned in, not of the effect.
//
// This is the fresh authoring job that note asks for, and it inverts the
// construction rather than retuning it:
//
//   a bolt is a DARK CRACKED HUSK with light escaping through the fissures,
//   not a hot core with a soft glow around it.
//
// The mass is opaque crust, so the darkening budget (BRIGHT_BACKGROUND_VFX_SPEC
// §5.7) is structural — the effect cannot stop attenuating the background
// without ceasing to be itself. The saturated hue lives in a grazing corona
// (§5.5's third term) and in the fissure light; white appears only at the
// leading nose, 1-3 px, as §5.4 requires. See core/shaders/rift_bolt.fs.
//
// NO DEBRIS LAYER. The first version shed one spark trail per flake off the
// back of the husk. It was cut on the owner's call on 27/08/2026 together with
// SPARK TRAIL itself — the primary is gone, not aliased, so there is nothing
// here to re-enable. A bolt that needs debris needs a debris primitive that
// does not read as an additive dash.
//
// IT DOES OWN ITS WAKE (27/08/2026, owner's call). The head spawns a stack of
// swept ribbons on the caller's own transform and releases them with the same
// lifecycle, so a skill gets head + wake for one call and still owns only one
// Matrix. The stack, its sizes and why they are ratios rather than metres are
// in the k_riftWake block below; `rift_bolt_wake_scale = 0` measures the head
// alone.
//
// SCALE. `radius` is the husk radius in metres and the authoring band is
// 0.10-0.16, default 0.12 — inside root CLAUDE.md's 0.10-0.20 mesh band, and
// about half fire_ball's 0.25 m combat collider, so the bolt sits INSIDE its
// own hitbox. It first shipped at 0.35 (0.70 m across, a beach ball) and that
// was simply out of band; there is nothing else in this tree at that size that
// a hand throws.
//
#include "core/tuning.h"
#include "core/gfx_quality.h"

#define VFX_RIFT_BOLT_MAX 6

// A bolt is small on screen next to a shield, so its ceiling is lower and its
// budget tighter. Same law as ShieldShell_Rings: the tier sets a ceiling, the
// live count spends what the budget has, and a lone bolt — which is exactly
// when its silhouette is biggest — gets the round outline for free.
typedef struct {
    bool active, stopping, hasPrev;
    const Matrix *xf;      // caller-owned, must outlive the handle
    VC_MaterialId mat;
    Vector3 pos, prevPos, heading;
    float radius, speed, level, target, elapsed;
    int shield;            // the FlowShield instance that IS the head
    int wake[3];           // swept ribbon stack, k_riftWake rows; -1 = not granted
    // One follow transform per strand, written every frame from the bolt's own
    // basis. These must live IN THE POOL: the trail system retains the pointer
    // for the ribbon's whole life, so a local or a caller temporary would
    // dangle. Also why Kill releases the ribbons before the slot is reused.
    Matrix wakeXf[3];
    float wakeAngle[3];    // integrated helix angle; see RiftBolt_AdvanceWake
} VC_RiftBolt;

static VC_RiftBolt s_riftBolts[VFX_RIFT_BOLT_MAX];
static bool s_riftBoltInit = false;

static float s_riftLightScale    = 4.2f;   // VFXLight radius, in husk radii
// ── THE WAKE: one straight spine plus TWO loose spiralling threads ─────────
//
// The owner's shape, 27/08/2026: "1 trail main dài không xoắn, 3 trail mỏng nhỏ
// độ dài khác nhau xoắn spiral nhưng xoắn tự nhiên chứ không được đều đặn quá."
// It is TWO threads and not three, and that is a measured concession rather
// than a shortcut — see the pixel budget below.
//
//   - ROW 0, the SPINE, rides the flight axis dead straight and is the longest
//     thing in the effect. It is what says where the bolt has been. Nothing
//     about it twists; a spine that wanders is not a spine.
//   - ROWS 1-2, the THREADS, are thinner, shorter than the spine and shorter
//     than each other (0.78 / 0.48 s), on opposite sides of their own helices
//     about that axis at 0.45 and 1.00 of the shell's RADIUS.
//
// ── WHY TWO AND NOT THREE: THE RADIAL BUDGET ──────────────────────────────
// Three constraints compete for one number, the room between the axis and the
// shell's surface. At the authored 0.08 m radius, in the harness's own pixels
// (the head is 40 px across, so 250 px/m):
//
//   * a thread must stay INSIDE the shell or it visibly detaches from the ball
//     -> its orbit is capped at 1 radius = 20 px;
//   * a thread must not narrow to sub-pixel or it BREAKS INTO DASHES
//     (core/docs/LANDMINES.md, 29/07) -> its ribbon wants 5 px at the very
//     least, and reads properly nearer 8;
//   * for N threads to read as N rather than as one rope, the SPACING between
//     their orbits has to exceed the ribbon WIDTH.
//
// Three threads therefore demand 15-21 px of the 20 px available, which is why
// the first cut came out as a single braided rope — its ribbons were 10-14 px
// wide on orbits 8-16 px apart, i.e. each ribbon was about as wide as the whole
// circle it travelled on. Two threads at 9 and 20 px, 10 and 8 px wide, have
// 11 px of clear air between them. Three would need roughly twice the room,
// which means a shell around 0.14 m, and the head size was already settled.
//
// Widths are multiples of the shell's DIAMETER and twist radii of its RADIUS,
// and every length is in SECONDS, so `radius` rescales the whole wake and the
// drawn length scales with the bolt's speed for free. The widths are CEILINGS:
// the swept presets cap width against the distance the tip has actually
// travelled, so a bolt that has barely moved gets threads rather than stubs.
//
// The threads use MAIN at thread width rather than WISP. WISP's `strand.gain`
// is the only one above 1 in the swept table, which pushes the sheet's authored
// hairs APART — correct for an inner core running inside another ribbon, and a
// row of dots when it IS the ribbon. Measured: three dotted arcs.
//
// ── "XOẮN TỰ NHIÊN, KHÔNG ĐỀU ĐẶN QUÁ" ────────────────────────────────────
// A constant-rate circle is a machined spring. Four things break that up, none
// of them noise for its own sake:
//
//   1. THE RATES ARE NOT HARMONIC (22 and 27 rad/s on 0.45 and 1.00 radii, half
//      a turn apart). Nothing divides anything else, so the two never re-align
//      into a visible repeat — the same reason k_sweptStrand detunes its wave
//      fields rather than scaling them.
//   2. THE ANGULAR RATE WANDERS on its own slow cycle, so a thread speeds up
//      and eases off instead of ticking.
//   3. THE ORBIT RADIUS BREATHES, INWARD ONLY, so the thread weaves toward the
//      spine and back out rather than tracing a perfect cylinder — a cylinder
//      is what a coil IS. Inward only because a symmetric swing would make the
//      authored radius a MIDPOINT, and a thread written at 1.00 would then
//      spend half its life outside the shell it is supposed to come out of.
//   4. THE PITCH LAW clamps all of it against the bolt's actual speed — see
//      RIFT_BOLT_PITCH_RATIO.
static const struct {
    TrailPresetId preset;
    float widthMul;    // x husk DIAMETER, full width — a CEILING, see below
    float life;        // seconds of tail memory (1.0 s TRAIL_HISTORY_COUNT cap)
    // Orbit radius about the flight axis, x the shell's RADIUS — NOT its
    // diameter, which is what this said until 27/08/2026 and is why the owner
    // reported the threads "tách rời khỏi quả cầu". A thread orbiting 0.78
    // DIAMETERS out is 1.56 radii from the axis on a sphere that ends at 1.0:
    // all three circles were entirely OUTSIDE the shell and never touched it.
    // Kept at or under 1.0 here (breathe included) so every thread is born on
    // or inside the surface and reads as coming OUT of the bolt.
    float twistMul;
    float twistSpin;   // rad/s ceiling about that axis
    float twistPhase;  // where on the circle this strand starts
    float wanderRate;  // Hz: how fast its ANGULAR rate drifts
    float breatheRate; // Hz: how fast its ORBIT RADIUS drifts
} k_riftWake[3] = {
    //                       width  life  twist  spin  phase   wander breathe
    { TRAIL_PRESET_MAIN,     0.80f, 0.95f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f },
    { TRAIL_PRESET_MAIN,     0.24f, 0.78f, 0.45f, 22.0f, 0.00f, 0.37f, 0.53f },
    { TRAIL_PRESET_MAIN,     0.19f, 0.48f, 1.00f, 27.0f, PI,    0.61f, 0.29f },
};

// WHICH RIBBONS SURVIVE A CUT. The rows above are in DRAW order; this is the
// PRIORITY order, and the spine is always first — a bolt granted one ribbon
// gets MAIN, never a lone thread spiralling around nothing. Threads are then
// added longest-first, so what a crowded frame loses is the shortest, faintest
// one.
static const int k_riftWakePick[4][3] = {
    {-1, -1, -1},    // 0
    { 0, -1, -1},    // 1: the spine alone
    { 0,  1, -1},    // 2: spine + the longer thread
    { 0,  1,  2},    // 3: the full stack
};

#define RIFT_BOLT_WAKE_BUDGET 6

static float s_riftWakeScale     = 1.0f;   // 0 disables the wake entirely
static float s_riftWakeFullSpeed = 6.0f;   // m/s at which the wake is full width

static float RiftBolt_Clamp01(float v)
{ return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// THE PITCH LAW, and it is the difference between a twist and a zigzag.
//
// A helix's tangential speed is radius x spin. Let that approach the forward
// speed and the strand is moving sideways about as fast as it is moving along,
// so consecutive nodes step across the path instead of down it — measured on
// the first cut (1.00 diameter at 24 rad/s against ~5 m/s), the ribbon left the
// husk, shot to a hard point and folded back: a chevron welded to the head, not
// a corkscrew. `twistSpin` in the table above is therefore a CEILING, and the
// rate actually used is whatever keeps
//
//     radius * spin <= RIFT_BOLT_PITCH_RATIO * speed
//
// i.e. a pitch angle under about twenty degrees. The consequence worth having:
// a bolt that slows down stops twisting instead of thrashing in place, and a
// fast one gets the full authored rate.
//
// Because the rate varies, the angle must be INTEGRATED rather than computed as
// elapsed * spin — the latter jumps whenever the clamp moves, which is a visible
// snap in the strand.
#define RIFT_BOLT_PITCH_RATIO 0.35f

static void RiftBolt_AdvanceWake(VC_RiftBolt *b, float dt)
{
    Vector3 fwd = b->heading;
    Vector3 up0 = (fabsf(fwd.y) < 0.94f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                         : (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up0, fwd));
    Vector3 up = Vector3CrossProduct(fwd, right);
    for (int row = 0; row < 3; ++row)
    {
        // RADIUS, not diameter: the surface of the shell is at 1.0 of this, so
        // an orbit above 1.0 leaves the sphere behind entirely.
        float base = b->radius * k_riftWake[row].twistMul;
        if (base <= 0.0001f)
        {
            // The SPINE. It rides the axis dead straight — row 0 has twistMul 0
            // and this is the branch it takes.
            b->wakeXf[row] = MatrixTranslate(b->pos.x, b->pos.y, b->pos.z);
            continue;
        }

        // BREATHE: the circle swells and shrinks, so the thread weaves toward
        // the spine and back out instead of tracing a cylinder. A cylinder is
        // what a coil spring IS, and it is the single thing that made the first
        // version read as machined.
        // BREATHE INWARD ONLY. A symmetric +/- swing means the authored radius is
        // a MIDPOINT, so a thread written at 1.0 spends half its time outside
        // the shell it is supposed to be coming out of — the detachment bug in
        // slow motion. Folding the sine to [0,1] and subtracting makes the
        // authored number a CEILING instead, which is what "on the surface"
        // has to mean.
        float breathe = 0.5f + 0.5f * sinf(b->elapsed * k_riftWake[row].breatheRate * 2.0f * PI
                                           + k_riftWake[row].twistPhase);
        float orbit = base * (1.0f - 0.30f * breathe);

        // WANDER: the angular rate itself drifts, so a thread accelerates and
        // eases rather than ticking at one speed.
        //
        // APPLIED AFTER THE CLAMP, NOT BEFORE, and that ordering is the whole
        // difference between this working and not. Written as
        // `min(authored * wander, ceiling)` the wander was ERASED: at any
        // ordinary bolt speed all three threads sit above their ceiling, so the
        // clamp won every frame and every thread ran at exactly
        // RIFT_BOLT_PITCH_RATIO * speed / orbit — one rate, no drift, and the
        // three "detuned" rows in the table above never reached a pixel.
        // Scaling the ALLOWED rate instead keeps the pitch law intact (the
        // factor never exceeds 1) while the variation survives the clamp.
        float ceiling = RIFT_BOLT_PITCH_RATIO * b->speed / fmaxf(orbit, 0.0001f);
        float spin = fminf(k_riftWake[row].twistSpin, ceiling);
        spin *= 0.72f + 0.28f * sinf(b->elapsed * k_riftWake[row].wanderRate * 2.0f * PI
                                     + k_riftWake[row].twistPhase * 1.7f);
        b->wakeAngle[row] += spin * dt;
        Vector3 p = VC_MotionOrbitAxis(b->pos, right, up, orbit,
                                       b->wakeAngle[row] + k_riftWake[row].twistPhase);
        b->wakeXf[row] = MatrixTranslate(p.x, p.y, p.z);
    }
}

static int RiftBolt_WakeLayers(void)
{
    if (s_riftWakeScale <= 0.0f) return 0;
    int live = 0, committed = 0;
    for (int i = 0; i < VFX_RIFT_BOLT_MAX; ++i)
    {
        if (!s_riftBolts[i].active) continue;
        live++;
        for (int L = 0; L < 3; ++L)
            if (s_riftBolts[i].wake[L] >= 0) committed++;
    }
    int want = (GfxQuality_Get() <= GFX_LOW) ? 1
             : (live == 0) ? 3 : (live == 1) ? 2 : 1;
    if (committed + want > RIFT_BOLT_WAKE_BUDGET)
        want = RIFT_BOLT_WAKE_BUDGET - committed;
    return want < 0 ? 0 : want;
}

static void RiftBolt_InitShared(void)
{
    if (s_riftBoltInit) return;
    Tuning_RegisterFloat("rift_bolt_light_scale", &s_riftLightScale,    4.2f);
    Tuning_RegisterFloat("rift_bolt_wake_scale",  &s_riftWakeScale,     1.0f);
    Tuning_RegisterFloat("rift_bolt_wake_speed",  &s_riftWakeFullSpeed, 6.0f);
    s_riftBoltInit = true;
}

/* THE HEAD SITS WHERE ITS CENTRE IS, AND FlowShield DOES NOT ASSUME THAT.
 *
 * VFX_FlowShield_Spawn/SetTransform take a GROUND point and lift the sphere
 * `radius * SHIELD_BURIED_LIFT` (0.5) above it, because the shield it was
 * written for stands on terrain three quarters out of the ground. A projectile
 * has no ground under it, so the lift has to be cancelled here or every bolt
 * flies half a radius above the path its own wake is laid on — and the wake
 * would be the thing that looks wrong, which is a bad place to start looking. */
static Vector3 RiftBolt_ShieldAnchor(const VC_RiftBolt *b)
{
    Vector3 p = b->pos;
    p.y -= b->radius * 0.5f;
    return p;
}

int VFX_ComposeRiftBolt(const Matrix *followTransform, VC_MaterialId mat, float radius)
{
    if (followTransform == NULL) return -1;
    RiftBolt_InitShared();
    for (int i = 0; i < VFX_RIFT_BOLT_MAX; ++i)
    {
        if (s_riftBolts[i].active) continue;
        // COUNTED BEFORE THE BOLT IS INSERTED, and it has to be: the ladder in
        // RiftBolt_WakeLayers reads `active`, so asking after the assignment
        // below counts this bolt as one of the OTHERS. A lone bolt then took
        // the two-bolt rung and got the spine plus exactly ONE thread — which
        // is what the owner saw and reported ("tôi chỉ thấy 1 sợi xoắn"). Not a
        // phase collision; the other two ribbons were never spawned.
        int layers = RiftBolt_WakeLayers();
        Vector3 p = { followTransform->m12, followTransform->m13, followTransform->m14 };
        s_riftBolts[i] = (VC_RiftBolt){
            .active = true, .xf = followTransform, .mat = mat,
            .pos = p, .prevPos = p,
            .heading = (Vector3){ 0.0f, 0.0f, 1.0f },
            .radius = radius > 0.0f ? radius : 0.08f,
            .target = 1.0f, .level = 0.0f
        };
        VC_RiftBolt *b = &s_riftBolts[i];
        b->wake[0] = b->wake[1] = b->wake[2] = -1;

        // THE HEAD IS A FlowShield, not a shader of this file's own (owner,
        // 27/08/2026: "bản rỗng thì cứ dùng flow shield thôi sao phải phức
        // tạp?"). It was right — vc_flow_shield.inl already owns the hollow
        // two-wall membrane, the far-then-near draw order and the refraction,
        // and rift_bolt.fs had grown a second copy of all three. The second
        // copy is deleted rather than kept behind a flag.
        b->shield = VFX_FlowShield_Spawn(RiftBolt_ShieldAnchor(b), mat,
                                         b->radius, 1.0f);

        // Each strand follows its OWN transform, not the caller's, because each
        // one is on a different point of the helix. The skill still owns only
        // the one Matrix it already had; these three are derived from it here
        // and rewritten every frame in the update loop.
        const float diameter = b->radius * 2.0f;
        RiftBolt_AdvanceWake(b, 0.0f);
        for (int n = 0; n < layers; ++n)
        {
            int row = k_riftWakePick[layers][n];
            if (row < 0) continue;
            b->wake[row] = VFX_ComposeTrail(
                &b->wakeXf[row], mat,
                diameter * k_riftWake[row].widthMul * s_riftWakeScale,
                k_riftWake[row].life, k_riftWake[row].preset);
        }
        return i;
    }
    return -1;
}

void VFX_RiftBolt_SetIntensity(int handle, float intensity01)
{
    if (handle < 0 || handle >= VFX_RIFT_BOLT_MAX || !s_riftBolts[handle].active) return;
    s_riftBolts[handle].target = RiftBolt_Clamp01(intensity01);
    s_riftBolts[handle].stopping = false;
    if (s_riftBolts[handle].shield >= 0)
        VFX_FlowShield_SetIntensity(s_riftBolts[handle].shield,
                                    s_riftBolts[handle].target);
    // The wake width is NOT set here. It is driven once per frame in the update
    // loop from level AND speed together; setting it from both places would let
    // whichever ran last win, which is the class of bug that reads as "the
    // tunable does nothing sometimes".
}

// Stops the FEED, not the pixels: the husk dims out over its own ramp so a bolt
// that reaches its target does not blink out of existence mid-frame.
void VFX_RiftBolt_Stop(int handle)
{
    if (handle < 0 || handle >= VFX_RIFT_BOLT_MAX || !s_riftBolts[handle].active) return;
    s_riftBolts[handle].stopping = true;
    s_riftBolts[handle].target = 0.0f;
    // Stop, not Kill: the shell fades over its own ramp, exactly like the
    // ribbons drain over theirs. The handle is dropped here because
    // VFX_FlowShield_Stop is what releases the slot once it reaches zero.
    if (s_riftBolts[handle].shield >= 0)
    {
        VFX_FlowShield_Stop(s_riftBolts[handle].shield);
        s_riftBolts[handle].shield = -1;
    }
    // Stop, never Kill: VFX_Trail_Stop ends the FEED and lets the laid plume
    // drain and fade on its own. Cutting it here would delete the tail of a
    // bolt that is still visibly dimming, which is the one thing a wake must
    // not do.
    for (int L = 0; L < 3; ++L)
        if (s_riftBolts[handle].wake[L] >= 0)
        {
            VFX_Trail_Stop(s_riftBolts[handle].wake[L]);
            s_riftBolts[handle].wake[L] = -1;
        }
}

void VFX_KillRiftBolt(int handle)
{
    if (handle < 0 || handle >= VFX_RIFT_BOLT_MAX) return;
    // The cancellation cut takes the wake with it: the transform the trail
    // follows is the CALLER's, and a killed bolt's owner is free to let that
    // Matrix go out of scope on the same frame.
    if (s_riftBolts[handle].shield >= 0) VFX_KillFlowShield(s_riftBolts[handle].shield);
    s_riftBolts[handle].shield = -1;
    for (int L = 0; L < 3; ++L)
    {
        if (s_riftBolts[handle].wake[L] >= 0) VFX_KillTrail(s_riftBolts[handle].wake[L]);
        s_riftBolts[handle].wake[L] = -1;
    }
    s_riftBolts[handle].active = false;
}

static void VC_RiftBolt_Update(float dt)
{
    if (dt <= 0.0f) return;
    for (int i = 0; i < VFX_RIFT_BOLT_MAX; ++i)
    {
        VC_RiftBolt *b = &s_riftBolts[i];
        if (!b->active) continue;
        b->elapsed += dt;

        // The heading is DERIVED from the transform, never passed in. A skill
        // that steers its projectile only has to move the matrix it was already
        // moving, and the bolt turns with it — same contract as the trails,
        // which sample `followTransform`'s origin for exactly this reason.
        Vector3 p = { b->xf->m12, b->xf->m13, b->xf->m14 };
        Vector3 delta = Vector3Subtract(p, b->prevPos);
        float dist = Vector3Length(delta);
        if (b->hasPrev && dist > 1.0e-4f)
        {
            float k = 1.0f - expf(-dt * 10.0f);
            Vector3 dir = Vector3Scale(delta, 1.0f / dist);
            b->heading = Vector3Normalize(Vector3Lerp(b->heading, dir, k));
            b->speed += ((dist / dt) - b->speed) * k;
        }
        b->hasPrev = true;
        b->prevPos = p;
        b->pos = p;

        b->level += (b->target - b->level) * (1.0f - expf(-dt * 9.0f));
        if (b->stopping && b->level < 0.004f)
        {
            b->active = false;
            // already Stopped in VFX_RiftBolt_Stop; shell and ribbons both
            // drain themselves from there
            b->shield = -1;
            b->wake[0] = b->wake[1] = b->wake[2] = -1;
            continue;
        }
        if (b->level <= 0.02f) continue;

        // THE WAKE ANSWERS TO SPEED, not just to being alive. A plume is
        // material left BEHIND: a bolt barely moving has not left any, and a
        // full-width tail on a near-stationary head is the "long straight
        // streak bridging two places" artefact in slow motion. The trail
        // system already refuses to lay nodes below `idleSpeed`; this is the
        // continuous version of the same statement, so the wake thins in as
        // the bolt accelerates and thins out as it stops instead of switching.
        // VFX_TrailSetWidth only moves the TARGET — Update ramps toward it — so
        // calling it every frame is the intended use, not a pop.
        // ADVANCE THE HELIX FIRST. VC_SweptTrail_Update ran earlier in this same
        // VFX_Compose_Update, so the ribbons sample what is written here on the
        // NEXT frame — one frame of lag, which every trail in this engine has
        // (the sandbox fixture writes its own transform during Draw) and which
        // a wake is the one consumer that cannot be hurt by: it is behind the
        // head by construction.
        RiftBolt_AdvanceWake(b, dt);

        {
            float speed01 = (s_riftWakeFullSpeed > 0.01f)
                                ? b->speed / s_riftWakeFullSpeed : 1.0f;
            speed01 = RiftBolt_Clamp01(speed01);
            float w = b->level * (0.35f + 0.65f * speed01);
            for (int L = 0; L < 3; ++L)
                if (b->wake[L] >= 0) VFX_TrailSetWidth(b->wake[L], w);
        }

        // Carry the head. FlowShield samples nothing on its own — it is a
        // positioned effect, not a follower — so this is what makes it fly.
        if (b->shield >= 0)
            VFX_FlowShield_SetTransform(b->shield, RiftBolt_ShieldAnchor(b));

        const VFX_ElementMaterial *m = VFX_Material(b->mat);

        // The pool of light a bolt throws on what it flies past. Spawned per
        // frame with a lifetime just over one, the standard continuous-emitter
        // pattern (vc_character_aura.inl, vc_flow_shield.inl), so it follows a
        // moving bolt and dies with it instead of needing its own handle.
        VFXLight_Spawn(b->pos, m->glow,
                       b->radius * s_riftLightScale * (0.55f + 0.45f * b->level),
                       0.09f, VFX_PRIORITY_LOW);
    }
}

/* NO GEOMETRY OF ITS OWN — and the pair is still required.
 *
 * `VC_<Name>_Update` + `VC_<Name>_Draw3D` IS how a stateful composition declares
 * itself to scripts/sync_vfx_test.py; the scan looks for both, and dropping this
 * half would silently unregister the pool that Update ticks. The flow shield
 * keeps its own Draw3D for a related reason (its refraction runs in a later
 * pass), so an empty one with the reason written down is the precedent here.
 *
 * The head is a FlowShield instance, drawn by VFX_FlowShield_DrawRefraction from
 * main.c after the 3D pass. The wake is four trails, drawn by the trail system.
 * Neither is this file's to submit. */
static void VC_RiftBolt_Draw3D(Camera3D cam)
{
    (void)cam;
}
