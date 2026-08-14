// Headless contract test — P4 ShieldShell.
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(c, n) do { if (c) printf("PASS: %s\n", n); else { printf("FAIL: %s\n", n); failures++; } } while (0)

#define SHIELD_MAX 8
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
    CHECK(HdrCrestGain(0.50f, 0.0f) < 0.001f,
          "sub-crest membrane stays below the HDR/bloom path");
    CHECK(HdrCrestGain(1.0f, 0.0f) > 1.0f,
          "only a hot crest clears the HDR bloom threshold");

    const char *src = "core/composition/common/vc_shield_shell.inl";
    CHECK(Has(src, "PlasmaMaterial_Load"), "NULL surface uses PlasmaMaterial fallback");
    CHECK(Has(src, "shield_shell_hdr_flow") && Has(src, "VC_Whiten(mat->glow, 0.72f)"),
          "fallback supplies a white HDR crest rather than only a rim");
    CHECK(!Has(src, "AuraShellMaterial"), "aura shell is not a ShieldShell fallback");
    CHECK(Has(src, "ResourceManager_LoadShader"), "authored shield shader goes through ResourceManager");
    CHECK(Has(src, "VFX_Material(shield->mat)"), "element colour comes from VFX_Material");
    CHECK(Has(src, "SetShaderValueTexture"), "surface body/flow/mask bind as shader inputs");
    // Migrated from FlowMap to core/uv's SurfaceFlow: same two-phase read, but
    // as layer 0 of an N-layer flow, so the membrane can be given more layers
    // from VFX_ShieldSurface data without editing the shader. The clock check
    // is not incidental — FlowMap_Apply used to push the time uniform as a
    // side effect and SurfaceFlow has no such hook, so losing that line would
    // freeze the membrane while every other assertion here stayed green.
    CHECK(Has(src, "SurfaceFlow_Apply") && Has(src, "SurfaceFlow_CacheLocations") &&
          Has(src, "s_shieldShader.time = GetShaderLocation(shader, \"uTime\");"),
          "shield delegates two-phase flow binding to core/uv, and still drives its own clock");
    CHECK(Has(src, "rlDrawRenderBatchActive"), "render-state changes are bracketed by flushes");
    CHECK(Has(src, "VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false") &&
          !Has(src, "VFX_SURFACE_MULTIPLIED") && !Has(src, "VFX_SURFACE_ADDITIVE"),
          "the whole shield alpha-composites without a darkening pass");
    CHECK(Has(src, "rlEnableBackfaceCulling") && !Has(src, "rlDisableBackfaceCulling"),
          "shield draws one membrane instead of accumulating its back face as haze");
    CHECK(Has("core/shaders/plasma_shell.fs", "float crest = smoothstep(0.74, 0.94, wisp)") &&
          !Has("core/shaders/plasma_shell.fs", "mix(0.20, 1.0, interiorFlow)"),
          "HDR is restricted to filament crests before ACES tone mapping");
    CHECK(Has("core/shaders/shield_shell.fs", "SurfaceFlow_FieldSample") &&
          Has("core/shaders/shield_shell.fs", "u_maskTex"),
          "authored path has distinct flow and mask channels");
    CHECK(Has("core/shaders/shield_shell.fs", "float strand = smoothstep(0.16, 0.62, detail)") &&
          !Has("core/shaders/shield_shell.fs", "calcFresnel") &&
          !Has("core/shaders/shield_shell.fs", "fragTexCoord"),
          "surface alpha is strand-driven and its mapping has no Fresnel pole seam");
    CHECK(Has("core/shaders/shield_shell.fs", "Shield_TriplanarBody") &&
          Has("core/shaders/shield_shell.fs", "pow(abs(normal), vec3(4.0))"),
          "body and flow map use a pole-free triplanar projection");
    CHECK(Has(src, "rlSetTexture(shield->surface.body.id)"),
          "authored body is bound to the immediate geometry batch");
    CHECK(Has("sandbox/vfx_test.c", "VFX_ShieldShell_SpawnEx(pos, VC_MAT_FIRE, 1.5f, 1.0f,") &&
          Has("sandbox/vfx_test.c", "VFXTest_ShieldFlowSurface()") &&
          Has("sandbox/vfx_test.c", "assets/textures/energy_volume.png") &&
          Has("sandbox/vfx_test.c", "assets/textures/energy_volume_flow.png"),
          "shield fixture supplies a separate body and RG flow-map profile");

    return failures ? 1 : 0;
}
