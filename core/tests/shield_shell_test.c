// Headless contract test — P4 ShieldShell.
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

#define SHIELD_MAX 8
/* The HIGHEST tier, because the budget has to hold at the worst case. Mirrors
   SHIELD_SHELL_RINGS_HIGH; the shell is tier-gated and LOW keeps 14x14. */
#define SHIELD_RINGS 20
#define SHIELD_SLICES 20
#define HDR_FLOW_GAIN 5.0f

static float Step(float level, float target, float dt)
{ return level + (target - level) * (1.0f - expf(-dt * 7.0f)); }

static float SmoothStep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static float HdrCrestGain(float wisp, float fresnel)
{ return HDR_FLOW_GAIN * SmoothStep(0.74f, 0.94f, wisp) * (0.35f + 0.65f * (1.0f - fresnel)); }

// Mirrors of the recipe's shader math (glass_shell.fs) — guarded against drift
// by the source-string checks on "m * m * m * m" and the smoothstep contact
// band below. The mirror cannot validate the noise-tap or depth-sampling paths
// (GPU only); it pins the closed-form terms: pow-4 fresnel and the
// depth-intersection contact profile.
static float GlassFresnelPow4(float cosTheta)
{
    float m = 1.0f - (cosTheta < 0.0f ? 0.0f : (cosTheta > 1.0f ? 1.0f : cosTheta));
    return m * m * m * m;
}

/* CUBIC, not `1 - smoothstep`. smoothstep has zero derivative at its lower edge, which
   gave the ground line a FLAT TOP: 13 px of pinned R where no luminance gradient is left
   and only hue can vary, which the post FX hue-restoration blend then swung into visible
   colour rings. The cubic falls away immediately, so the pinned core is a sliver. */
/* Screen half-width of the rim band, as a FRACTION of the sphere's screen radius.
 *
 * This is the guard for "the two rims are not the same size". The rim rides
 * pow(1 - |N.V|, u_rimPower), and on a sphere |N.V| = sqrt(1 - s^2) with s = r/R, so the
 * band's half-maximum sits at a screen radius that follows straight from the exponent —
 * no camera, no scene, no render needed. It is the one number that says how WIDE the
 * silhouette reads, and the whole complaint was that it read twice the ground line.
 *
 * Amplitude deliberately does not appear: width at half maximum is scale-invariant for a
 * fixed profile, which is why sweeping u_rimStrength 2.15 -> 0.55 moved the measured band
 * 18 px -> 16 px and nothing else. Only the exponent moves this. */
static float GlassRimBandHalfWidthFrac(float power)
{
    float mHalf = powf(0.5f, 1.0f / power);       /* (1-|N.V|) where the band is half-max */
    float ndotv = 1.0f - mHalf;
    float s = sqrtf(1.0f - ndotv * ndotv);        /* r/R at that point */
    return 1.0f - s;                              /* half-width, in units of R */
}

static float GlassContactMirror(float shellDepth, float sceneDepth, float thickness)
{
    float gap = sceneDepth - shellDepth;
    if (gap <= 0.0f) return 0.0f; // occluded, or depth texture holds no data
    float t = gap / thickness;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float m = 1.0f - t;
    return m * m * m;
}

/* The old plateau, kept so the no-flat-top assertion below is shown to discriminate. */
static float GlassContactPlateau(float shellDepth, float sceneDepth, float thickness)
{
    float gap = sceneDepth - shellDepth;
    if (gap <= 0.0f) return 0.0f;
    float t = gap / thickness;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 1.0f - (t * t * (3.0f - 2.0f * t));
}

/* The source text from the emission render scope to the end of the draw function. Checks
   about the emission pass have to be scoped to it, or the body pass's own front/back pair
   satisfies them and the guard never fails. */
static const char *EmissionBlock(const char *path)
{
    static char text[120000];
    FILE *file = fopen(path, "rb");
    if (!file) return "";
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    const char *start = strstr(text, "VFX_RENDER_PASS_EMISSION");
    return start ? start : "";
}

static int CountIn(const char *haystack, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);
    for (const char *p = haystack; (p = strstr(p, needle)) != NULL; p += len) n++;
    return n;
}

static int EmissionBlockHas(const char *path, const char *needle)
{ return strstr(EmissionBlock(path), needle) != NULL; }

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    static char text[120000];
    size_t count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    // Stop is a real wind-down, never a popped shell or a held pool slot.
    float level = 0.0f;
    for (int i = 0; i < 60; ++i) level = Step(level, 1.0f, 1.0f / 60.0f);
    CHECK(level > 0.99f, "spawn reaches visible intensity");
    for (int i = 0; i < 90; ++i) level = Step(level, 0.0f, 1.0f / 60.0f);
    CHECK(level < 0.004f, "Stop fades far enough to release its slot");

    CHECK(SHIELD_MAX * SHIELD_RINGS * SHIELD_SLICES * 2 <= 6400,
          "eight alpha shields stay inside the 6400-triangle primary budget");
    /* Assert the BUDGET and the tier ladder, not one literal constant. Pinning "14"
       is what made a legitimate tier gate look like a regression: the number is an
       implementation detail, while "LOW still fits the mobile budget" and "the top
       tier still fits the eight-shield budget" are the actual contracts. */
    CHECK(Has("core/composition/common/vc_shield_shell.inl", "SHIELD_SHELL_RINGS_LOW 14"),
          "LOW tier still holds the 200-400 triangle mobile force-field budget");
    CHECK(Has("core/composition/common/vc_shield_shell.inl",
              "activeCount * (rings + 2) * (rings + 2) * 2 <= SHIELD_SHELL_TRI_BUDGET"),
          "above LOW, tessellation spends the aggregate budget against the LIVE count");
    CHECK(Has("core/composition/common/vc_shield_shell.inl", "ShieldShell_Rings(int activeCount)"),
          "tessellation is chosen per quality tier and live count, not fixed");
    CHECK(HdrCrestGain(0.50f, 0.0f) < 0.001f,
          "sub-crest membrane stays below the HDR/bloom path");
    CHECK(HdrCrestGain(1.0f, 0.0f) > 1.0f,
          "only a hot crest clears the HDR bloom threshold");

    // Recipe mirrors: fresnel falls off as pow(1 - dot(N,V), 4).
    CHECK(GlassFresnelPow4(1.0f) < 0.0001f, "face-on fresnel is zero");
    CHECK(fabsf(GlassFresnelPow4(0.5f) - 0.0625f) < 0.0001f, "mid-angle fresnel is (0.5)^4");
    CHECK(fabsf(GlassFresnelPow4(0.0f) - 1.0f) < 0.0001f, "grazing fresnel is one");
    // Contact: 1 at a real intersection, fading over the thickness band,
    // zero when the scene is in front (occluded) or far away.
    CHECK(GlassContactMirror(5.0f, 5.0f + 1e-5f, 0.35f) > 0.999f, "contact peaks at the intersection");
    CHECK(GlassContactMirror(5.0f, 8.0f, 0.35f) < 0.0001f, "no contact far from geometry");
    CHECK(GlassContactMirror(5.0f, 4.5f, 0.35f) < 0.0001f, "no contact when the scene occludes the shell");
    /* NO FLAT TOP. This is the shape guard for the colour-ring bug: a band that holds ~1
       for the first tenth of its width pins the R channel over a wide stripe, and hue is
       then the only thing left that can vary across it. One tenth into the band the
       profile must already have given up real coverage. The plateau form scores 0.972
       there and would pass any "peaks at the intersection" check, so assert the contrast
       against it rather than a bare threshold. */
    {
        const float tenth = 5.0f + 0.035f;   /* 10% of a 0.35 m band */
        float peaked = GlassContactMirror(5.0f, tenth, 0.35f);
        float plateau = GlassContactPlateau(5.0f, tenth, 0.35f);
        CHECK(peaked < 0.80f && plateau > 0.95f,
              "the contact profile has no flat top (and the old plateau form would fail this)");
        printf("      contact at 10%% of the band: peaked %.3f vs plateau %.3f\n", peaked, plateau);
    }
    /* Monotone all the way out — a non-monotone profile is a ring by construction. */
    {
        int monotone = 1;
        float prev = 2.0f;
        /* From a hair past zero, not from zero: gap == 0 is the OCCLUDED branch and
           returns 0, which is a different thing from "the band starts here". */
        for (int i = 0; i <= 40; ++i) {
            float v = GlassContactMirror(5.0f, 5.0f + 1e-5f + 0.35f * (float)i / 40.0f, 0.35f);
            if (v > prev) monotone = 0;
            prev = v;
        }
        CHECK(monotone, "contact falls monotonically across the whole band");
    }

    /* THE TWO RIMS MUST BE THE SAME SIZE — the owner's report, turned into a number.
       Measured on the fixture before the fix: silhouette 16 px wide with a ~5 px white
       core, ground contact 8 px with a ~1 px core. The cause was structural, not a value:
       the contact is a cubic over a distance the author sets, while the silhouette rode a
       FIXED quartic whose screen width is whatever the sphere's radius makes it. With the
       exponent exposed as `shield_shell_rim_power`, the band is authorable.
       The old quartic scores 0.0127 here and would fail this check, which is what makes it
       a guard rather than a restatement of the current value. */
    {
        const float shipped = GlassRimBandHalfWidthFrac(8.0f);
        const float quartic = GlassRimBandHalfWidthFrac(4.0f);
        CHECK(shipped < 0.005f && quartic > 0.010f,
              "the rim band is narrow enough to match the ground line (and the old quartic would fail)");
        printf("      rim half-width vs sphere radius: power 8 = %.4f, old power 4 = %.4f\n",
               (double)shipped, (double)quartic);
    }
    /* THE RIM'S WHITE CORE MUST NOT RIDE `rimBand`, however symmetric that would be with
       the contact line. It was tried and shipped for one round, and the silhouette came
       back blotchy: peak luminance along the bottom arc scattered with sd 10.6 against 4.3
       for the form below. `wallDensity` is built on 1 / max(|N.V|, 0.10), so it SATURATES
       a few pixels before the silhouette and the white core cannot chase anything;
       `rimBand` is (1-|N.V|)^8 with no clamp, so it carries every sub-pixel wobble of the
       rasterised edge — amplified by an eighth power — into a white-vs-hue mix.
       Four other explanations were measured and rejected first (tessellation 40 -> 96:
       no change; the pattern term: 10.0; the matcap: 11.2; both wall passes overlapping:
       8.4). See glass_shell.fs for the full list; the point of this check is that the
       symmetric-looking version must not come back. */
    {
        const char *fs = "core/shaders/glass_shell.fs";
        CHECK(Has(fs, "smoothstep(0.90, 0.998, wallDensity) * 0.25") &&
              !Has(fs, "smoothstep(0.75, 1.0, rimBand)"),
              "the rim's white core rides a quantity that SATURATES at the silhouette");
        /* ONE hot colour for both rims, not two that happen to be tuned alike. The
           silhouette's peak measured 252,248,220 (chroma 0.125) against the ground line's
           255,224,183 (0.282) while their saturated bodies already agreed to 1/255 — the
           gap was whitening, not hue. Sharing `u_contactColor` is what keeps them matched
           when the palette changes; a tuned multiplier on u_rimColor measures the same
           today (total error 41 vs 40 over three pixels) and would drift tomorrow. */
        CHECK(Has(fs, "mix(u_contactColor.rgb, vec3(1.0), smoothstep(0.90, 0.998, wallDensity)"),
              "the silhouette takes the SAME hot colour the ground line uses");
        CHECK(Has(fs, "1.0 / max(shieldNdotV, 0.10)"),
              "...and that saturation is the path-length clamp, not an accident");
        CHECK(Has(fs, "smoothstep(0.75, 1.0, contact)"),
              "the contact band still keys its white on its own peak (a screen-space term, no clamp needed)");
        CHECK(Has(fs, "pow(fresnelM, u_rimPower)") && !Has(fs, "rimHot * (fresnel * u_rimStrength)"),
              "the rim band has its own authorable width, separate from the wall's fresnel");
    }

    const char *src = "core/composition/common/vc_shield_shell.inl";
    CHECK(Has(src, "ResourceManager_LoadShader") && Has(src, "glass_shell.fs"),
          "shield uses the dedicated glass shell shader");
    CHECK(!Has(src, "PlasmaMaterial_Load") && !Has(src, "SurfaceFlow_Apply") &&
          !Has(src, "Material_LoadCustom"),
          "legacy plasma, flow, and opaque material shell are removed");
    CHECK(Has(src, "VFX_Material(shield->mat)"), "element colour comes from VFX_Material");
    CHECK(Has(src, "SkillManager_BeginShader") && Has(src, "SkillManager_EndShader"),
          "glass shader owns its uniforms and batching");
    CHECK(Has(src, "rlDrawRenderBatchActive"), "render-state changes are bracketed by flushes");
    CHECK(Has(src, "VFX_RENDER_PASS_BODY, VFX_APPEARANCE_MAGIC") &&
          Has(src, "VFX_RENDER_PASS_EMISSION, VFX_APPEARANCE_MAGIC") &&
          !Has(src, "VFX_SURFACE_MULTIPLIED"),
          "the shell uses the shared Magic body+emission appearance");
    CHECK(Has(src, "rlEnableBackfaceCulling") &&
          Has(src, "RL_CULL_FACE_BACK") && Has(src, "RL_CULL_FACE_FRONT"),
          "mobile shell composites back then front glass interfaces");
    /* BOTH INTERFACES IN BOTH PASSES. The emission scope used to run one draw and inherit
       the body pass's cull state, so it covered the near wall only: the far wall received
       the body pass — which only takes light out — and no radiance at all, which is why
       it rendered as a colourless dark shape and why its ground line drew as a BLACK rim
       (the contact term still raised that wall's alpha, but its glow never reached the
       framebuffer). Scoped to the emission block so this cannot be satisfied by the body
       pass's own pair of draws. */
    CHECK(EmissionBlockHas(src, "RL_CULL_FACE_FRONT") &&
          EmissionBlockHas(src, "RL_CULL_FACE_BACK") &&
          CountIn(EmissionBlock(src), "ShieldShell_DrawPass(true)") == 2,
          "the emission pass composites BOTH glass interfaces, not the near wall alone");
    CHECK(CountIn(EmissionBlock(src), "s_shieldShader.wallPass") == 2,
          "each emission interface declares which wall it is drawing");
    CHECK(Has(src, "SceneTargets_RequestSceneSnapshot") &&
          Has(src, "SceneTargets_GetSceneSnapshotTexture") &&
          Has(src, "SceneTargets_GetDepthTexture") &&
          Has(src, "SetShaderValueTexture"),
          "shell uses the safe scene snapshot and gates optional depth");
    CHECK(Has("core/scene_targets.h", "SceneTargets_GetDepthTexture"),
          "the optional depth source remains available through ScreenDistort");
    CHECK(Has("main.c", "SceneTargets_SnapshotDepth();") &&
          Has("main.c", "VFX_ShieldShell_DrawRefraction(camera)"),
          "the shield draws in the existing post-3D composition pass");
    CHECK(Has("core/composition/visual_composer.h", "VFX_ShieldShell_DrawRefraction") &&
          Has("core/composition/common/vc_shield_shell.inl", "void VFX_ShieldShell_DrawRefraction"),
          "the dedicated refraction pass is exported for main.c");
    /* Pin the CONTRACT, not an empty body: the archetype pair must exist for
       sync_vfx_test.py, and the shell must not DRAW from it (the real draw is the
       post-3D refraction pass). The stub since gained a legitimate non-drawing job —
       requesting the soft-depth region, which only counts if it happens inside the 3D
       pass — and pinning the literal text made that read as a regression. */
    CHECK(Has("core/composition/common/vc_shield_shell.inl",
              "static void VC_ShieldShell_Draw3D(Camera3D cam)") &&
          !Has("core/composition/common/vc_shield_shell.inl",
               "DrawCoreSphere(shield->pos, radius, rings, rings, WHITE);\n}"),
          "the archetype pair stays for sync_vfx_test.py; the real draw moved "
          "to the post-3D refraction pass");
    /* WAS "per vertex", and that was the bug, not the contract. A quartic sampled at the
       vertices and interpolated linearly across a 40-slice sphere is flat facets joined by
       creases, and wall density, path length, the rim's white threshold and the emission
       mask all hang off it — so the whole shell terraced along the mesh rings. The
       contract is the multiply-chain pow-4; the STAGE it runs in is now the fragment one. */
    CHECK(Has("core/shaders/glass_shell.fs", "u_emissionOnly") &&
          Has("core/shaders/glass_shell.fs", "fresnelX2 * fresnelX2"),
          "Fresnel uses multiply-chain math, evaluated per fragment");
    CHECK(Has("core/shaders/glass_shell.fs", "VFX_ResolvePremultiplied") &&
          Has("core/shaders/glass_shell.fs", "VFX_ResolveEmission"),
          "the shell resolves body and emission through the shared compositor");
    CHECK(Has("core/shaders/glass_shell.fs", "bodyStructure") &&
          Has(src, "appearance.bodyOpacity") &&
          Has(src, "appearance.emissionIntensity"),
          "Magic appearance drives structured body coverage and emission gain");
    CHECK(Has("core/shaders/glass_shell.fs", "float emissionMask") &&
          Has("core/shaders/glass_shell.fs", "pattern * 0.35") &&
          !Has("core/shaders/glass_shell.fs", "0.20 + fresnel"),
          "emission has no full-sphere alpha floor on bright backgrounds");
    CHECK(Has("core/shaders/glass_shell.fs", "u_packedTex") &&
          Has("core/shaders/glass_shell.fs", "flowSample.rg") &&
          Has("core/shaders/glass_shell.fs", "packed.b"),
          "flow vector and energy mask use the packed texture");
    CHECK(Has("core/shaders/glass_shell.fs", "u_flowTex") &&
          Has("core/shaders/glass_shell.fs", "u_hasFlow"),
          "legacy body plus RG flow-map fixtures remain supported");
    CHECK(Has("core/shaders/glass_shell.fs", "u_lightDirView") &&
          Has(src, "Environment_GetSunDirection") && Has(src, "lightView"),
          "glass body and rim include the camera-relative environment light");
    CHECK(Has(src, "wallPass") && Has("core/shaders/glass_shell.fs", "u_wallPass") &&
          Has("core/shaders/glass_shell.fs", "wallWeight"),
          "front and back glass interfaces receive separate optical weights");

    // The glass recipe: fresnel = pow(1 - saturate(dot(N,V)), 4), contact from
    // the scene-depth intersection, refraction via a noise-jittered scene tap,
    // alpha = base + fresnel*fresnelAlpha + contact*contactAlpha.
    CHECK(Has("core/shaders/glass_shell.fs", "shieldPow4") &&
          Has("core/shaders/glass_shell.fs", "fresnelX2 * fresnelX2"),
          "glass fresnel uses the recipe's multiply-chain falloff");
    /* PER PIXEL. A quartic evaluated at the vertices and interpolated linearly across a
       40-slice sphere is a set of flat facets joined by creases, and every term the shell
       shades with hangs off it. The vertex stage must not bake it. */
    CHECK(!Has("core/shaders/glass_shell.vs", "shieldFresnel") &&
          !Has("core/shaders/glass_shell.vs", "shieldNdotV"),
          "the fresnel curve is evaluated per pixel, not interpolated from the vertices");
    /* Pin the CONTRACT — the band is the depth gap normalised by u_contactThickness —
       not the falloff curve. The literal `smoothstep(0.0, u_contactThickness` was pinned
       here, which made replacing a flat-topped profile with a peaked one read as a
       regression in two unrelated checks. The curve is a look; the normalisation is the
       contract. The C mirror above is what guards the curve's shape. */
    CHECK(Has("core/shaders/glass_shell.fs", "depthContact") &&
          Has("core/shaders/glass_shell.fs", "u_depthTex") &&
          Has("core/shaders/glass_shell.fs", "gap / u_contactThickness"),
          "contact term reads the depth intersection against the shell surface");
    CHECK(Has("core/shaders/glass_shell.fs", "u_hasDepth") &&
          Has("core/shaders/glass_shell.fs", "u_depthEnabled") &&
          Has("core/shaders/glass_shell.fs", "gap / u_contactThickness"),
          "depth intersection is optional and quality-gated");
    CHECK(Has("core/shaders/glass_shell.fs", "impactRipple") &&
          Has("core/shaders/glass_shell.fs", "sin(d * u_rippleFrequency") &&
          Has("core/shaders/glass_shell.fs", "exp(-d) * exp(-u_impactAge)"),
          "impact ripple uses bounded wave and exponential decay");
    CHECK(Has("core/shaders/glass_shell.fs", "u_parallaxDepth") &&
          Has("core/shaders/glass_shell.fs", "shieldViewDir.xy * u_parallaxDepth"),
          "inner energy uses view-dependent parallax");
    /* THE GROUND LINE CARRIES ONE HUE, and only the RIM gets an authored white core.
       A white core was tried on the contact band and reverted: at the silhouette the
       white sits on a real luminance rise so it reads as "hotter", but the ground line is
       already at the ceiling across its whole width (8 consecutive pixels with R pinned
       at 255), so white there cannot brighten anything — it only shifts hue, and the eye
       reads a separate pale stripe with a hard edge inside the orange one. Term-by-term
       ablation isolated it: removing the matcap or the rim's white changed nothing,
       removing this made the band a single continuous gradient. Guard both halves so
       neither is re-derived. */
    /* BOTH bands get a white core — the ground line is the same event as the silhouette
       and must look like it. What matters is the WINDOW: copying `rimHot`'s narrow
       0.88..0.995 put the white on a shell of `contact` values that, after the profile
       went cubic, sits OFF the visible band's peak, so it read as a separate pale stripe
       beside the orange one instead of a hot core inside it. Assert that the contact
       window starts materially lower than the rim's, which is the property that keeps the
       whitest pixel on the brightest pixel. */
    CHECK(Has("core/shaders/glass_shell.fs",
              "mix(u_contactColor.rgb, vec3(1.0), smoothstep(0.75, 1.0, contact))"),
          "the contact band has a white core on a window matched to its own peak");
    /* Was: `mix(u_rimColor.rgb, vec3(1.0), smoothstep(` — the silhouette having its OWN
       hot colour is exactly what the owner reported as the two rims not matching, so the
       invariant is now that it shares the contact's. Kept as a check rather than deleted
       so a revert to a private rim colour trips something. */
    CHECK(Has("core/shaders/glass_shell.fs",
              "mix(u_contactColor.rgb, vec3(1.0), smoothstep(") &&
          !Has("core/shaders/glass_shell.fs", "mix(u_rimColor.rgb, vec3(1.0), smoothstep("),
          "the silhouette rim no longer keeps a private white core of its own");
    /* And it survives the far wall's attenuation: the contact line belongs to the surface
       the shell touches, not to which wall you are looking at, and half of it lives on
       the far wall. Adding it AFTER the rearInterface scale is what keeps that half. */
    CHECK(Has("core/shaders/glass_shell.fs", "contactGlow") &&
          Has("core/shaders/glass_shell.fs", "glow += contactGlow;"),
          "the contact glow is added after the rear attenuation, not scaled by it");
    CHECK(Has("core/shaders/glass_shell.fs", "u_matcapTex") &&
          Has("core/shaders/glass_shell.fs", "normal.xy * 0.5 + 0.5"),
          "outer shell supports view-normal matcap reflection");
    CHECK(Has("core/shaders/glass_shell.fs", "bottomGlow") &&
          Has("core/shaders/glass_shell.fs", "-normal.y"),
          "lower hemisphere receives the characteristic green energy bloom");

    CHECK(Has("CMakeLists.txt", "configure_file(core/shaders/glass_shell.fs") &&
          Has("CMakeLists.txt", "configure_file(core/shaders/glass_shell.vs"),
          "glass shader stages are copied into desktop build trees");
    CHECK(Has("sandbox/vfx_test.c", "VFX_ShieldShell_SpawnEx(pos, VC_MAT_FIRE, 1.5f, 1.0f,") &&
          Has("sandbox/vfx_test.c", "VFXTest_ShieldFlowSurface()") &&
          Has("sandbox/vfx_test.c", "assets/textures/energy_volume.png") &&
          Has("sandbox/vfx_test.c", "assets/textures/energy_volume_flow.png"),
          "shield fixture supplies a separate body and RG flow-map profile");

    return failures ? 1 : 0;
}
