// ── E5.3 — VFX_ComposeChargeConverge ─────────────────────────────────────────
//
// THE CAST TELL, AND NOW A PURE SCORE. Threads of qi lift off the surface of a
// sphere and are drawn into a hot core. Pairs with VFX_SeqPreset's anticipation
// phase (E3).
//
// It has no visual idea of its own left, and that is the finished state rather
// than an erosion. It gave up its DESTINATION on 29/07, when the eighty inlined
// lines of three stacked sprites plus a point light became
// `VFX_ComposeCoreGlow`; it gave up its MOTES on 30/07 (P2), when the emitter
// sphere, the attractor field and the thread particles became
// `VFX_ComposeConvergeMotes`. Both were verbatim moves — only the address
// changed — which is the whole argument for extracting from an approved
// composite: it costs no visual iteration. `core/tests/converge_motes_test.c`
// exists to prove that claim rather than assert it.
//
// A composite should be a score over primaries and nothing else
// (core/docs/VFX_PLAN.md §Part 4). What is left here is the score: WHICH
// primaries, at WHAT scale, and under WHAT gate.
//
// The plan sketched the score as three calls — "motes + CoreGlow + light". It is
// two, because the light travelled WITH the glow when the glow was extracted:
// it is the same point of light, it runs on the glow's own 0.07 s timer, and
// firing a second one here would double it. Two calls is the finished shape.

// ── RE-SCORED 28/08/2026 — THE DESTINATION IS A BALL, NOT A POINT ───────────
//
// Owner's reference: threads appear at random in the space around and are drawn
// into A SPHERE. The score names two primaries — the streamers, and the ball
// they are drawn into.
//
// THE BALL IS `VFX_ComposeFlowShield`, and there is NO core glow behind it any
// more (owner, 28/08/2026: *"quả cầu thì dùng flow shield ko cần lõi sáng
// nữa"*). Both parts of that are load-bearing. FlowShield is a lit vein field
// over a hollow membrane — it is bright THROUGHOUT, so it needs nothing burning
// inside it; ShieldShell, which this used first, is refracted glass that is dark
// until something lights it, which is why that version needed a core and still
// read as a bubble with a spark in it.
//
// AND THAT MAKES THIS SCORE STATEFUL, which it was not before. A shield shell is
// a handle with a lifetime, not a fire-and-forget spawn, so this file now owns a
// small pool: one shell per converge CENTRE, matched by position exactly the way
// the motes match their force fields, plus the per-frame tick that stops a shell
// whose converge has stopped being fed. The tick has to exist independently of
// the compose call — the caller signals "done" by NOT CALLING, so a teardown
// that lived in the compose function could never run. `VC_ChargeConverge_Update`
// and `VC_ChargeConverge_Draw3D` are the archetype pair that declares this file
// stateful to scripts/sync_vfx_test.py, which wires both into
// VFX_Compose_Update/Draw3D.

#define CHARGE_MAX_SHELLS 4
// How long a converge may go unfed before its shell is stopped. Long enough to
// survive a dropped frame or a one-frame gap between two casts, short enough
// that a released charge does not leave a ball hanging in the air.
#define CHARGE_SHELL_IDLE 0.25f

typedef struct {
    bool    used;
    int     handle;
    Vector3 pos;
    float   idle;      // seconds since this centre was last fed
} ChargeShellSlot;

static ChargeShellSlot s_chargeShells[CHARGE_MAX_SHELLS];

// The one dial the SCORE owns: whether this converge has a destination at all.
// A drain or an absorb is the same indraught with nothing at the middle of it.
static float s_chargeCore = 1.0f; // 0 = no ball at the centre
//
// The ball's SIZE is deliberately not here. It is the streamers' sink radius —
// the distance at which they stop feeding — and the two have to be one number or
// the ribbons either vanish early inside the ball or stop short of it with a
// visible gap. `charge_ball` therefore lives in vc_converge_motes.inl and is
// read back through VC_ConvergeMotesSinkFrac().
static bool s_chargeCoreInit = false;

static void ChargeConverge_InitShared(void)
{
    if (s_chargeCoreInit) return;
    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (docs/LANDMINES.md).
    Tuning_RegisterFloat("charge_core", &s_chargeCore, 1.0f);
    for (int i = 0; i < CHARGE_MAX_SHELLS; i++) s_chargeShells[i].handle = -1;
    s_chargeCoreInit = true;
}

// The per-frame tick. Its whole job is the teardown the compose call cannot do:
// a caller stops a charge by no longer calling, so "unfed for CHARGE_SHELL_IDLE"
// is the only signal that a converge has ended. Stop() fades the shell out;
// Kill() would pop it.
static void VC_ChargeConverge_Update(float dt)
{
    for (int i = 0; i < CHARGE_MAX_SHELLS; i++)
    {
        ChargeShellSlot *sl = &s_chargeShells[i];
        if (!sl->used) continue;
        sl->idle += dt;
        if (sl->idle > CHARGE_SHELL_IDLE)
        {
            if (sl->handle >= 0) VFX_FlowShield_Stop(sl->handle);
            sl->used = false;
            sl->handle = -1;
        }
    }
}

// Deliberately empty, and it still has to exist: the archetype scan takes the
// Update/Draw3D PAIR as the declaration that a file is stateful, so the pair is
// how this file gets its tick. Nothing is drawn here — the shell draws itself
// through its own pool, and the motes and the glow are particles.
static void VC_ChargeConverge_Draw3D(Camera3D cam)
{
    (void)cam;
}

// THE SHIELD'S `pos` IS A GROUND POINT, NOT A CENTRE. Both shield compositions
// lift the sphere by half its radius and leave the bottom quarter below the
// plane through `pos`, because the first job of either was a bubble standing on
// the floor around a character's feet. A charge ball FLOATS, so the same
// convention buries a quarter of it — VFX_ComposeRiftBolt cancels it the same
// way for the same reason (see RiftBolt_ShieldAnchor).
//
// The lift is read from the shell's own macro rather than copied as a number:
// every .inl lands in one translation unit and vc_shield_shell.inl is included
// first, so this compiles against whatever the shell currently uses instead of
// silently disagreeing with it.
static Vector3 ChargeConverge_ShellGroundPoint(Vector3 center, float ballR)
{
    center.y -= ballR * SHIELD_BURIED_LIFT;
    return center;
}

// One shell per converge centre. Slots are matched by position — the same rule
// as the motes' force-field pool, and for the same reason: two converges at once
// must not share one ball.
static void ChargeConverge_Ball(Vector3 center, VC_MaterialId mat, float ballR, float t01)
{
    // MATCHING A MOVING CONVERGE TO ITS OWN BALL. The motes' force-field pool can
    // match on a fixed 0.5 m because a mismatch there costs one frame of one
    // thread pulling the wrong way. Here a mismatch spawns a SECOND ball and
    // leaves the first to time out, so a caster who walks while charging would
    // shed balls behind them and exhaust the pool. So: nearest used slot within a
    // couple of ball radii — which is far more than a converge can move in one
    // frame and far less than the distance at which two casters read as one.
    float   bestD2 = (2.5f * ballR) * (2.5f * ballR);
    int     slot   = -1;
    for (int i = 0; i < CHARGE_MAX_SHELLS; i++)
    {
        if (!s_chargeShells[i].used) continue;
        float d2 = Vector3DistanceSqr(s_chargeShells[i].pos, center);
        if (d2 < bestD2) { bestD2 = d2; slot = i; }
    }

    if (slot < 0)
    {
        for (int i = 0; i < CHARGE_MAX_SHELLS; i++)
            if (!s_chargeShells[i].used) { slot = i; break; }
        if (slot < 0) return;               // four converges at once is the cap
        // The shell's own radius is fixed at spawn, so it is spawned at the size
        // it will hold; `intensity` is what rises with the charge.
        s_chargeShells[slot].handle = VFX_ComposeFlowShield(
            ChargeConverge_ShellGroundPoint(center, ballR), mat, ballR, t01);
        s_chargeShells[slot].used   = true;
        s_chargeShells[slot].pos    = center;
    }

    s_chargeShells[slot].idle = 0.0f;
    s_chargeShells[slot].pos  = center;
    if (s_chargeShells[slot].handle >= 0)
    {
        VFX_FlowShield_SetTransform(s_chargeShells[slot].handle,
                                    ChargeConverge_ShellGroundPoint(center, ballR));
        VFX_FlowShield_SetIntensity(s_chargeShells[slot].handle,
                                    Math_Mix(0.35f, 1.0f, t01));
    }
}

// Continuous: call once per frame while the charge is winding up.
// `radius`    = the scale of the whole tell in metres: threads appear between
//               1.15 and 3 of it out, and the ball at the middle is `charge_ball`
//               of it.
// `t01`       = charge progress 0→1: density, pull, brightness, ball intensity.
// `moteCount` = threads per second.
void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius,
                               float t01, int moteCount)
{
    ChargeConverge_InitShared();
    if (radius <= 0.0f)
        radius = 1.0f;
    if (t01 < 0.0f)
        t01 = 0.0f;
    if (t01 > 1.0f)
        t01 = 1.0f;

    // WHERE THE TELL ACTUALLY SITS. The ball is placed TANGENT above the point
    // the caller names, not centred on it (owner: "quả cầu nên hoàn toàn trên
    // mặt đất"), so a converge fired at a character's feet or at a map point puts
    // the whole sphere in the air rather than half of it in the floor.
    //
    // The streamers move with it. Their hub is what they fall toward and it has
    // to be the ball's CENTRE, or they would converge on a point at its bottom
    // edge and pour into the ground beside it.
    float   ballR = radius * VC_ConvergeMotesSinkFrac() * VC_ConvergeMotesSizeMul();
    Vector3 hub   = { center.x, center.y + ballR, center.z };

    // 1. THE STREAMERS — qi condensing out of the space around and swept inward.
    VFX_ComposeConvergeMotes(hub, mat, radius, t01, moteCount);

    // 2. THE DESTINATION — the ball the streamers are drawn into. `charge_core`
    //    still means "does this converge have a destination at all": a drain or
    //    an absorb is the same indraught with nothing at the middle of it.
    if (s_chargeCore > 0.5f && t01 > 0.05f)
        ChargeConverge_Ball(hub, mat, ballR, t01);
}
