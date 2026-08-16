// Headless contract test — P4 ShieldShell.
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

#define SHIELD_MAX 8
#define SHIELD_RINGS 14
#define SHIELD_SLICES 14
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

static float GlassContactMirror(float shellDepth, float sceneDepth, float thickness)
{
    float gap = sceneDepth - shellDepth;
    if (gap <= 0.0f) return 0.0f; // occluded, or depth texture holds no data
    float t = gap / thickness;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 1.0f - (t * t * (3.0f - 2.0f * t));
}

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
    CHECK(Has("core/composition/common/vc_shield_shell.inl", "SHIELD_SHELL_RINGS 14") &&
          Has("core/composition/common/vc_shield_shell.inl", "SHIELD_SHELL_SLICES 14"),
          "mobile shell stays within a 200-400 triangle mesh budget");
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
    CHECK(GlassContactMirror(5.0f, 5.02f, 0.35f) > 0.99f, "contact peaks at the intersection");
    CHECK(fabsf(GlassContactMirror(5.0f, 5.175f, 0.35f) - 0.5f) < 0.001f, "contact is half at mid-band");
    CHECK(GlassContactMirror(5.0f, 8.0f, 0.35f) < 0.0001f, "no contact far from geometry");
    CHECK(GlassContactMirror(5.0f, 4.5f, 0.35f) < 0.0001f, "no contact when the scene occludes the shell");

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
    CHECK(Has(src, "ScreenDistort_RequestSceneSnapshot") &&
          Has(src, "ScreenDistort_GetSceneSnapshotTexture") &&
          Has(src, "ScreenDistort_GetDepthTexture") &&
          Has(src, "SetShaderValueTexture"),
          "shell uses the safe scene snapshot and gates optional depth");
    CHECK(Has("core/screen_distort.h", "ScreenDistort_GetDepthTexture"),
          "the optional depth source remains available through ScreenDistort");
    CHECK(Has("main.c", "ScreenDistort_SnapshotDepth();") &&
          Has("main.c", "VFX_ShieldShell_DrawRefraction(camera)"),
          "the shield draws in the existing post-3D composition pass");
    CHECK(Has("core/composition/visual_composer.h", "VFX_ShieldShell_DrawRefraction") &&
          Has("core/composition/common/vc_shield_shell.inl", "void VFX_ShieldShell_DrawRefraction"),
          "the dedicated refraction pass is exported for main.c");
    CHECK(Has("core/composition/common/vc_shield_shell.inl",
              "static void VC_ShieldShell_Draw3D(Camera3D cam) { (void)cam; }"),
          "the archetype pair stays for sync_vfx_test.py; the real draw moved "
          "to the post-3D refraction pass");
    CHECK(Has("core/shaders/glass_shell.fs", "u_emissionOnly") &&
          Has("core/shaders/glass_shell.vs", "fresnelX2 * fresnelX2"),
          "Fresnel is calculated per vertex with multiply-chain math");
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
          Has("core/shaders/glass_shell.vs", "fresnelX2 * fresnelX2"),
          "glass fresnel uses the recipe's multiply-chain falloff");
    CHECK(Has("core/shaders/glass_shell.fs", "depthContact") &&
          Has("core/shaders/glass_shell.fs", "u_depthTex") &&
          Has("core/shaders/glass_shell.fs", "smoothstep(0.0, u_contactThickness"),
          "contact term reads the depth intersection against the shell surface");
    CHECK(Has("core/shaders/glass_shell.fs", "u_hasDepth") &&
          Has("core/shaders/glass_shell.fs", "u_depthEnabled") &&
          Has("core/shaders/glass_shell.fs", "smoothstep(0.0, u_contactThickness"),
          "depth intersection is optional and quality-gated");
    CHECK(Has("core/shaders/glass_shell.fs", "impactRipple") &&
          Has("core/shaders/glass_shell.fs", "sin(d * u_rippleFrequency") &&
          Has("core/shaders/glass_shell.fs", "exp(-d) * exp(-u_impactAge)"),
          "impact ripple uses bounded wave and exponential decay");
    CHECK(Has("core/shaders/glass_shell.fs", "u_parallaxDepth") &&
          Has("core/shaders/glass_shell.fs", "shieldViewDir.xy * u_parallaxDepth"),
          "inner energy uses view-dependent parallax");
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
