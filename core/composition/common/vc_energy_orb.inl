// ── PRIMARY. VFX_ComposeEnergyOrb — a sphere that reads as a VOLUME ─────────
//
// The head of the owner's reference projectile (29/07): a ball whose INTERIOR is
// dark and whose LIMB is bright, with filaments crawling over its surface and a
// white-hot point at its centre. That silhouette — dark middle, bright edge — is
// what separates an orb from a glowing dot, and it is the one thing a sprite
// cannot fake from every angle.
//
// WHY NOT THE OBVIOUS THINGS, both already tried in this tree:
//
//   - NOT concentric additive shells alone (the black-hole technique). Additive
//     stacking is BRIGHTEST where you look through the most shells, which is the
//     centre — exactly backwards. Shells give turbulence, not a rim.
//   - NOT a translucent EffectMaterial sphere. That was proposed as a glass ball
//     and rejected: EffectMaterial's translucency has a 0.3 alpha floor, so it
//     cannot get out of its own way (docs/LANDMINES.md).
//
// A rim needs a FRESNEL term, and the tree already has one: `aura_shell.fs`,
// whose `fresnelPower` its own header documents as "higher = emptier center".
// It was written for buff-column cylinders and has had no consumer since — one
// of the primitives VFX_PLAN §0 lists as existing and unused, which is exactly
// the queue this is being taken from (extract, then WIRE, then invent).
//
// Two things it needs on a sphere that a cylinder never did:
//   1. `topY` pushed far above the orb. On a cylinder it fades the open top rim;
//      on a sphere any finite value slices the ball with a horizontal gradient.
//   2. Backface culling OFF. Seeing the far wall through the near one is most of
//      what makes it read as a shell rather than a disc — and additively, the
//      far wall's rim is what fills the silhouette's edge.
//
// The white-hot centre is `VFX_ComposeCoreGlow`, called rather than reimplemented.
// That primary was extracted an hour before this one was written, and this is the
// return on it: the hot point, its bloom-buying mid layer and its falloff all
// arrive here for one line and cannot drift out of sync with the charge's.
//
// IMMEDIATE MODE. Call every frame the orb should exist. It draws geometry, so
// there is nothing to accumulate — but the core glow inside it emits by rate.

#define ORB_SHELLS 1

static AuraShellMaterial s_orbShell;
static bool s_orbInit = false;
static float s_orbSize = 1.0f;   // x on the whole orb's radius
static float s_orbRim = 1.0f;    // x on rim brightness — the silhouette dial
static float s_orbCore = 1.0f;   // 0 = no hot centre, just the shell
// Base coverage the shell's noise modulates. This is the bright-background dial: on dark
// scenery an effect can be seen by adding light, on bright scenery only coverage makes
// contrast (§4/§5.7). 0 reproduces the old noise-gated film exactly.
static float s_orbCoverFloor = 0.30f;

// radiusScale · opacity · fresnelPower · scroll (sign = direction) · noiseScale.
//
// ONE SHELL, down from three (owner, 29/07: "one shell is enough"). The three
// were carrying a fresnel ramp — each shell emptier in the middle than the one
// inside it, so rim accumulated on rim — and that reasoning was sound and also
// unnecessary: a single fresnel term already produces a rim, and stacking three
// of them mostly produced a THICKER rim plus three sphere submissions.
//
// It also removes an honesty problem. With three shells the tier gate dropped
// the middle one; with one there is nothing to shed, so the gate now does
// nothing and says so, rather than looking like a budget control that is not.
static const struct
{
    float radiusScale, opacity, fresnelPower, scroll, noiseScale;
} k_orbShells[ORB_SHELLS] = {
    {1.00f, 0.90f, 3.2f, 1.4f, 4.2f},
};

static void Orb_InitShared(void)
{
    if (s_orbInit)
        return;
    AuraShellMaterialParams p = {
        .bodyColor = WHITE, // per-call, from the element material
        .glowColor = WHITE,
        .opacity = 1.0f,
        .fresnelPower = 3.0f,
        .rimStrength = 1.2f,
        .scrollSpeed = 1.2f,
        .noiseScale = 4.0f,
        .heightScale = 1.6f,
        .scanFreq = 9.0f,
        .scanSpeed = 1.3f,
        .scanStrength = 0.25f,
        .displaceAmp = 0.0f,
        // FAR above any orb. On a cylinder this fades the open top rim; on a
        // closed sphere a finite value cuts a horizontal gradient across the
        // ball and the top half goes missing.
        .topY = 1.0e6f,
    };
    AuraShellMaterial_Load(&s_orbShell, &p);

    // Lazily, never from a subsystem Init — Tuning_Init runs after those and an
    // early registration silently keeps the default (core/docs/LANDMINES.md).
    Tuning_RegisterFloat("orb_size", &s_orbSize, 1.0f);
    Tuning_RegisterFloat("orb_rim", &s_orbRim, 1.0f);
    Tuning_RegisterFloat("orb_core", &s_orbCore, 1.0f);
    Tuning_RegisterFloat("orb_cover_floor", &s_orbCoverFloor, 0.30f);
    s_orbInit = true;
}

void VFX_ComposeEnergyOrb(Vector3 center, VC_MaterialId mat, float radius,
                          float intensity01)
{
    Orb_InitShared();
    if (radius <= 0.0f)
        radius = 0.35f;
    if (intensity01 < 0.0f)
        intensity01 = 0.0f;
    if (intensity01 > 1.0f)
        intensity01 = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    float r = radius * s_orbSize;

    // NO TIER GATE. One sphere at 20x20 is 800 triangles submitted once, which
    // is not a budget worth gating, and the alternative — dropping the only
    // shell — would delete the effect rather than cheapen it. A gate that can
    // only turn something off is not a quality tier. (The rule it still obeys:
    // a gate may only ever clamp DOWN, so the honest move is not to have one.)

    // SURFACE MUST MATCH THE SHADER'S RESOLVER. aura_shell.fs returns
    // VFX_ResolveBody(col, 1.0, alpha) — a straight-alpha BODY — and
    // vfx_composite.glsl's contract is that ResolveBody is drawn with the ALPHA
    // surface. Bound ADDITIVE (as it was until 17/08/2026) the alpha is consumed as a
    // brightness multiplier and the coverage term is thrown away, so the orb could never
    // attenuate anything. That is not a theory: measured across five backgrounds it
    // darkened 0.0% of its own footprint on every one, and its body area collapsed from
    // 10.04% of the frame on a dark background to 0.07% on a white one — it effectively
    // ceased to exist in daylight. See BRIGHT_BACKGROUND_VFX_SPEC.md §11b; re-measure
    // with `scripts/render_vfx_matrix.sh "ENERGY ORB"`.
    //
    // The PASS is left as EMISSION deliberately: pass says what the draw contributes,
    // surface says how it combines (core/vfx_render.h), and those are orthogonal.
    VFXRenderScope renderScope = VFXRender_BeginDraw(
        VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ALPHA, false);
    // The far wall seen through the near one is most of what reads as a shell
    // rather than a disc.
    rlDisableBackfaceCulling();

    s_orbShell.params.bodyColor = m->body;
    s_orbShell.params.glowColor = m->glow;

    for (int i = 0; i < ORB_SHELLS; i++)
    {
        s_orbShell.params.opacity = k_orbShells[i].opacity * Math_Mix(0.55f, 1.0f, intensity01);
        s_orbShell.params.fresnelPower = k_orbShells[i].fresnelPower;
        s_orbShell.params.rimStrength = 1.2f * s_orbRim * Math_Mix(0.7f, 1.0f, intensity01);
        s_orbShell.params.scrollSpeed = k_orbShells[i].scroll;
        s_orbShell.params.noiseScale = k_orbShells[i].noiseScale;
        // This is a SPHERE. aura_shell.fs was written for a cylinder rising off the
        // ground and fades its alpha to zero toward the top; on a sphere that deletes
        // the upper half's coverage outright. And its alpha is noise-GATED, so most of
        // what is left is fully transparent. Both are why the orb measured 10.06% body
        // area on a dark background and 1.04% on a white one — see §11b.
        s_orbShell.params.heightFadeOff = 1.0f;
        s_orbShell.params.coverFloor = s_orbCoverFloor;
        AuraShellMaterial_Begin(s_orbShell);
        DrawCoreSphere(center, r * k_orbShells[i].radiusScale, 20, 20, WHITE);
        // Flush before the next shell's uniforms land, or the batch draws all
        // three with whichever set was written last (ENGINE_LANDMINES §1).
        rlDrawRenderBatchActive();
        AuraShellMaterial_End();
    }

    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    VFXRender_EndDraw(&renderScope);

    // The white-hot centre, and the reason CoreGlow was extracted first.
    if (s_orbCore > 0.5f)
        VFX_ComposeCoreGlow(center, mat, r * 0.9f, intensity01);
}
