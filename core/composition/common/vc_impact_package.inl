// VFX_ComposeImpactPackage — E6 #6. The impact, as a VFX_Sequence: a light
// flash, an energy/smoke burst, a distortion and a decal, timed as a track.
// That is E3's whole purpose — the offsets between the beats ARE the effect.
//
// WHAT IT DELIBERATELY NO LONGER HAS (owner, 28/07/2026: *"giờ impact chỉ cần
// khói, di chuyển theo tôi mô tả là đủ, ko cần gì hết"*):
//
//   - a separate DUST cloud. It was a second smoke population with its own
//     sheet, force field and colours, doing what the burst already does.
//   - ballistic DEBRIS sparks. They said "what was hit"; nothing asked for that
//     to be said, and they cluttered the read of the burst.
//   - velocity STRETCH on the burst sprites, which read as a trail smeared
//     behind each one.
//   - camera SHAKE — never added on a VFX's own initiative; a severity gate is
//     not permission.
//
// Each of those was reasoned from a reference rather than looked at. The
// package is stronger with one population and clear timing than with four
// populations competing inside a tenth of a second.
//
// SEVERITY IS ONE DIAL: it scales the burst and the flash together, and gates
// the beats that must not fire on a light hit (hitstop 0.45, distortion 0.35).

static bool s_ipInit = false;

// PER-BEAT KILL SWITCHES, added to settle a performance question by measurement
// instead of argument. The burst on its own holds ~60 fps under continuous
// fire; the package does not. The burst is the only beat made of particles, so
// the cost is in one of the others — and both of the plausible ones are the
// same shape of problem: a handful of objects whose cost is paid per FRAGMENT
// across the whole screen.
//
//   LIGHT   — every active VFX light is a loop iteration in every particle
//             fragment AND in every lit surface fragment in the scene.
//   DISTORT — the distortion pass always runs (it is the blit of the scene),
//             but with sources active its shader loops over them per fragment,
//             full screen.
//   DECAL   — one more textured quad; the cheap one, listed so it can be ruled
//             out rather than assumed.
//
// Turn them off one at a time in tuning.cfg and watch the frame rate; whichever
// restores it is the answer. They exist for the measurement, and they stay
// afterwards as the per-effect budget switches.
static float s_ipLight   = 1.0f;
static float s_ipDistort = 1.0f;
static float s_ipDecal   = 1.0f;
static float s_ipHitstop = 1.0f;

static void ImpactPkg_InitShared(void)
{
    if (s_ipInit)
        return;
    // Lazily, at first USE — never from a subsystem Init, or Tuning_Init runs
    // afterwards and silently keeps the defaults (docs/LANDMINES.md).
    Tuning_RegisterFloat("impact_light", &s_ipLight, 1.0f);
    Tuning_RegisterFloat("impact_distort", &s_ipDistort, 1.0f);
    Tuning_RegisterFloat("impact_decal", &s_ipDecal, 1.0f);
    Tuning_RegisterFloat("impact_hitstop", &s_ipHitstop, 1.0f);
    s_ipInit = true;
}

static void ImpactPkg_EnergyBurst(Vector3 pos, float scale, void *ud)
{
    VFX_ComposeEnergyBurst(pos, (VC_MaterialId)(intptr_t)ud, scale, 0.85f);
}

void VFX_ComposeImpactPackage(Vector3 pos, Vector3 normal, VC_MaterialId matId,
                              float scale, float severity01)
{
    ImpactPkg_InitShared();
    SmokePuff_InitShared();      // the fallback sprites, if the sheet is absent

    const float sev = Clamp(severity01, 0.0f, 1.0f);
    const VFX_ElementMaterial *mat = VFX_Material(matId);
    if (Vector3LengthSqr(normal) < 0.0001f)
        normal = (Vector3){0.0f, 1.0f, 0.0f};
    normal = Vector3Normalize(normal);

    // The normal has to outlive this call: the sequence fires its beats over
    // the next fraction of a second, long after this stack frame is gone.
    // One slot per sequence, so a burst of impacts cannot alias each other.
    static Vector3 s_ipNormals[VFX_SEQ_MAX];
    static int     s_ipNormalNext = 0;
    Vector3 *nrm = &s_ipNormals[s_ipNormalNext];
    s_ipNormalNext = (s_ipNormalNext + 1) % VFX_SEQ_MAX;
    *nrm = normal;

    // SEVERITY LIVES HERE, AND ONLY HERE. The sequence multiplies every beat's
    // spatial `a` by this scale (`s->scale * scale` for COMPOSE, `b->a *
    // s->scale` for LIGHT / DISTORT / DECAL), so a beat that ALSO ramps with
    // severity applies it twice.
    //
    // Measured, at severity 1.0: the burst was running at scale 1.25 x 1.35 =
    // 1.69, which is 123 sprites instead of 75, each 2.85x the area — 4.7x the
    // fill of the same burst fired on its own. That is the entire reason the
    // package dropped the frame rate while the burst alone held 60: it was
    // never the light, the distortion or the decal, it was the same effect at
    // nearly five times the size.
    //
    // Every `a` below is therefore a plain per-beat PROPORTION, in metres at
    // scale 1 and severity 1. Time values (hitstop, lifetimes) still ramp with
    // severity where it makes sense — the sequence does not scale those.
    VFX_Sequence *s = VFX_SeqBegin(pos, matId, scale * Math_Mix(0.7f, 1.25f, sev));
    if (s == NULL)
        return;

    // t=0 — THE FLASH. What tells the player the hit registered, and the only
    // beat that must never be delayed: everything else can arrive a frame or
    // two late without reading as lag.
    // E8: a VFX light is a loop iteration in EVERY particle and lit-surface
    // fragment on screen, so spamming impacts spreads its cost over the whole
    // frame (PROGRESS, 28/07). Below MED the flash is dropped, not dimmed.
    if (s_ipLight > 0.5f && GfxQuality_Get() >= GFX_MED)
    VFX_SeqAt(s, 0.0f, (VFX_Beat){
        .kind = VFX_BEAT_LIGHT,
        .a = 3.4f,                           // radius, metres (x sequence scale)
        .b = Math_Mix(0.10f, 0.22f, sev),    // lifetime — a TIME, not scaled
        .color = mat ? mat->glow : WHITE,
    });

    // t=0 — HITSTOP, gated. A light hit that freezes the game reads as a
    // dropped frame, not as weight.
    if (sev >= 0.45f && s_ipHitstop > 0.5f)
        VFX_SeqAt(s, 0.0f, (VFX_Beat){
            .kind = VFX_BEAT_HITSTOP,
            .a = Math_Mix(0.03f, 0.075f, sev),   // duration
            .b = 0.06f,                          // timeScale during it
        });

    // t=0.005 — THE ENERGY BURST. This is the impact, not the dust: the owner's
    // direction (28/07/2026) is that the energy explosion is what must read and
    // the dust merely accompanies it. It comes a hair after the flash so the
    // light is what the eye catches first.
    VFX_SeqAt(s, 0.005f, (VFX_Beat){
        .kind = VFX_BEAT_COMPOSE,
        .a = 1.0f,                           // the sequence scale is the size
        .cb = ImpactPkg_EnergyBurst,
        .ud = (void *)(intptr_t)matId,
    });

    // t=0.03 — distortion, gated: a shockwave on a light hit is noise.
    if (sev >= 0.35f && s_ipDistort > 0.5f && GfxQuality_Get() >= GFX_MED)
        VFX_SeqAt(s, 0.03f, (VFX_Beat){
            .kind = VFX_BEAT_DISTORT,
            .a = 2.1f,                        // radius (x sequence scale)
            .b = Math_Mix(0.18f, 0.42f, sev), // strength — not a length
            .c = 0.35f,                       // lifetime
        });

    // t=0.04 — the mark left behind, and the only beat that outlives the
    // moment. Its lifetime scales with severity because a scuff and a crater
    // should not linger for the same three seconds.
    if (s_ipDecal > 0.5f)
    VFX_SeqAt(s, 0.04f, (VFX_Beat){
        .kind = VFX_BEAT_DECAL,
        .a = 1.5f,                           // scale (x sequence scale)
        .b = Math_Mix(1.6f, 4.5f, sev),      // lifetime — a TIME, not scaled
        .c = (float)GetRandomValue(0, 360),  // rotation
    });

    // NO SCREEN SHAKE. This shipped with a shake beat gated at severity 0.90;
    // the owner's rule (28/07/2026) is that shake is never added on a VFX's own
    // initiative, and a gate is not permission. Shake is a whole-screen cost
    // paid by everything on screen, including what the player is reading, so it
    // is the owner's call and not a detail of this file. If a boss ultimate
    // wants it, the CALLER adds it.

    VFX_SeqPlay(s);
}
