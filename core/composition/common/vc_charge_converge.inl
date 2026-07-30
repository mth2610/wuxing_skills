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

// The one dial the SCORE owns, as opposed to the dials that belong to the pieces:
// whether this converge has a destination at all. A drain or an absorb is the
// same motes with no hot core, and that is a composition-level decision.
static float s_chargeCore = 1.0f;   // 0 = no hot core at the centre
static bool  s_chargeCoreInit = false;

// Continuous: call once per frame while the charge is winding up.
// `radius`    = the emitter sphere's radius in metres (where threads are born).
// `t01`       = charge progress 0→1: density, pull, brightness, light size.
// `moteCount` = threads per second.
void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius,
                               float t01, int moteCount)
{
    if (!s_chargeCoreInit)
    {
        // Lazily, never from a subsystem Init — Tuning_Init runs after those and
        // an early registration silently keeps the default (docs/LANDMINES.md).
        Tuning_RegisterFloat("charge_core", &s_chargeCore, 1.0f);
        s_chargeCoreInit = true;
    }
    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;

    // 1. THE MOTES — qi peeling off the shell and falling inward along threads.
    VFX_ComposeConvergeMotes(center, mat, radius, t01, moteCount);

    // 2. THE DESTINATION — one hot point of light, and the point light with it.
    //    `charge_size` scales both the threads and the glow, so the whole tell
    //    grows together rather than the core drifting out of proportion.
    if (s_chargeCore > 0.5f && t01 > 0.05f)
        VFX_ComposeCoreGlow(center, mat, radius * VC_ConvergeMotesSizeMul(), t01);
}
