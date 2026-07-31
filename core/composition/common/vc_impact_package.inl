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

// ── THE IMPACT PRIMARIES ────────────────────────────────────────────────────
//
// An impact is not one effect, it is four arriving on a schedule. Until now the
// four existed only as beats INSIDE this file, which had two costs. The obvious
// one is that a skill wanting just a flash had to fire a whole package. The
// expensive one is that a beat buried in a composite is invisible: the DECAL
// beat here shipped without its `.ud` texture, so the sequencer's
// `if (b->ud != NULL)` skipped it every time and the impact has been drawing
// NO MARK AT ALL — while the `normal` this function takes, and carefully kept
// alive in a static ring for that beat, was written and never read. Nothing
// failed, nothing logged, and it survived every review. A named primary with
// its own bench entry could not have hidden that.
//
// So the numbers below are the single definition of what an impact's pieces
// ARE, used both by the primaries and by the package's score. Severity is a
// deliberate exception: it scales TIMES here (a scuff and a crater should not
// linger equally) and never sizes — the size ramp lives in exactly one place,
// the package, or it multiplies (see the note in VFX_ComposeImpactPackage).
static float ImpactFlash_Radius(void)             { return 3.4f; }
static float ImpactFlash_Life(float sev)          { return Math_Mix(0.10f, 0.22f, sev); }
static float ImpactDistort_Radius(void)           { return 2.1f; }
static float ImpactDistort_Strength(float sev)    { return Math_Mix(0.18f, 0.42f, sev); }
static float ImpactDistort_Life(void)             { return 0.35f; }
static float ImpactDecal_Radius(void)             { return 1.5f; }
static float ImpactDecal_Life(float sev)          { return Math_Mix(1.6f, 4.5f, sev); }

// The tier gates travel WITH the primaries, not with the package. A caller
// reaching for the primary directly gets the same budget protection, which is
// the whole point of making these callable rather than copyable.
void VFX_ComposeImpactFlash(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    ImpactPkg_InitShared();
    float sev = Clamp(severity01, 0.0f, 1.0f);
    // E8: a VFX light is a loop iteration in EVERY particle and lit-surface
    // fragment on screen, so spamming impacts spreads its cost over the whole
    // frame. Below MED the flash is DROPPED, not dimmed.
    if (s_ipLight <= 0.5f || GfxQuality_Get() < GFX_MED) return;
    const VFX_ElementMaterial *m = VFX_Material(matId);
    VFXLight_Spawn(pos, m ? m->glow : WHITE,
                   ImpactFlash_Radius() * scale, ImpactFlash_Life(sev),
                   // Only two priorities exist. LOW: an impact is a normal beat
                   // and must never evict an ultimate's light.
                   VFX_PRIORITY_LOW);
}

void VFX_ComposeImpactDistort(Vector3 pos, float scale, float severity01)
{
    ImpactPkg_InitShared();
    float sev = Clamp(severity01, 0.0f, 1.0f);
    // The distortion pass costs per fragment across the WHOLE screen for every
    // live source, which is the shape of cost a Mali A33 cannot absorb.
    if (s_ipDistort <= 0.5f || GfxQuality_Get() < GFX_MED) return;
    ScreenDistort_Add(pos, ImpactDistort_Radius() * scale,
                      ImpactDistort_Strength(sev), ImpactDistort_Life(), 1.0f);
}

void VFX_ComposeImpactDecal(Vector3 pos, VC_MaterialId matId, float scale, float severity01)
{
    ImpactPkg_InitShared();
    if (s_ipDecal <= 0.5f) return;
    float sev = Clamp(severity01, 0.0f, 1.0f);
    // P4 Scorch owns Fire's ground mark. It is invoked from the event score,
    // never a per-frame draw path, so one impact creates exactly one lifecycle.
    if (matId == VC_MAT_FIRE)
    {
        VFX_ComposeScorch(pos, matId, ImpactDecal_Radius() * scale, sev);
        return;
    }
    // Routed through SpawnGroundDecal, which RESOLVES the preset to a real
    // texture. The dead beat this replaces passed no texture at all — that is
    // exactly the failure a preset enum prevents and a raw Texture2D field
    // invites.
    const VFX_ElementMaterial *m = VFX_Material(matId);
    DecalPresetType preset = (m && m->blendMode == BLEND_ALPHA) ? DECAL_PRESET_CRACK
                                                                : DECAL_PRESET_BURN;
    SpawnGroundDecal(preset, pos, ImpactDecal_Radius() * scale, ImpactDecal_Life(sev));
}

// Sequence adapters — the package fires the primaries THROUGH the score, so
// there is exactly one implementation of each beat and it is the callable one.
typedef struct { VC_MaterialId mat; float sev; } ImpactPkgParams;
static ImpactPkgParams s_ipParams[VFX_SEQ_MAX];
static int             s_ipParamNext = 0;

static void ImpactPkg_Flash(Vector3 pos, float scale, void *ud)
{ const ImpactPkgParams *p = (const ImpactPkgParams *)ud;
  VFX_ComposeImpactFlash(pos, p->mat, scale, p->sev); }
static void ImpactPkg_Distort(Vector3 pos, float scale, void *ud)
{ const ImpactPkgParams *p = (const ImpactPkgParams *)ud;
  (void)p; VFX_ComposeImpactDistort(pos, scale, p->sev); }
static void ImpactPkg_Decal(Vector3 pos, float scale, void *ud)
{ const ImpactPkgParams *p = (const ImpactPkgParams *)ud;
  VFX_ComposeImpactDecal(pos, p->mat, scale, p->sev); }

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

    // The beat parameters have to outlive this call: the sequence fires over the
    // next fraction of a second, long after this stack frame is gone. One slot
    // per sequence, so a burst of impacts cannot alias each other. (This ring
    // used to hold the `normal` for a decal beat that never fired — see the
    // primaries above. `normal` is still taken because callers pass a surface
    // and the API should not churn, but nothing consumes it yet: the decal path
    // is ground-projected, so there is no orientation to give it.)
    ImpactPkgParams *prm = &s_ipParams[s_ipParamNext];
    s_ipParamNext = (s_ipParamNext + 1) % VFX_SEQ_MAX;
    prm->mat = matId;
    prm->sev = sev;
    (void)normal;

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
    VFX_SeqAt(s, 0.0f, (VFX_Beat){
        .kind = VFX_BEAT_COMPOSE,
        .a = 1.0f,                 // the sequence scale IS the size
        .cb = ImpactPkg_Flash,     // gate + numbers live in the primary
        .ud = prm,
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
    if (sev >= 0.35f)
        VFX_SeqAt(s, 0.03f, (VFX_Beat){
            .kind = VFX_BEAT_COMPOSE,
            .a = 1.0f,
            .cb = ImpactPkg_Distort,
            .ud = prm,
        });

    // t=0.04 — the mark left behind, and the only beat that outlives the
    // moment. Its lifetime scales with severity because a scuff and a crater
    // should not linger for the same three seconds.
    VFX_SeqAt(s, 0.04f, (VFX_Beat){
        .kind = VFX_BEAT_COMPOSE,
        .a = 1.0f,
        .cb = ImpactPkg_Decal,
        .ud = prm,
    });

    // NO SCREEN SHAKE. This shipped with a shake beat gated at severity 0.90;
    // the owner's rule (28/07/2026) is that shake is never added on a VFX's own
    // initiative, and a gate is not permission. Shake is a whole-screen cost
    // paid by everything on screen, including what the player is reading, so it
    // is the owner's call and not a detail of this file. If a boss ultimate
    // wants it, the CALLER adds it.

    VFX_SeqPlay(s);
}
