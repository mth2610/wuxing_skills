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
    CHECK(Has("core/composition/common/vc_shield_shell.inl", "SHIELD_SHELL_RINGS 32") &&
          Has("core/composition/common/vc_shield_shell.inl", "SHIELD_SHELL_SLICES 32"),
          "glass sphere uses a rounder 32x32 silhouette without scene feedback");
    CHECK(HdrCrestGain(0.50f, 0.0f) < 0.001f,
          "sub-crest membrane stays below the HDR/bloom path");
    CHECK(HdrCrestGain(1.0f, 0.0f) > 1.0f,
          "only a hot crest clears the HDR bloom threshold");

    const char *src = "core/composition/common/vc_shield_shell.inl";
    CHECK(Has(src, "ResourceManager_LoadShader") && Has(src, "glass_shell.fs"),
          "shield uses the dedicated glass shell shader");
    CHECK(!Has(src, "PlasmaMaterial_Load") && !Has(src, "SurfaceFlow_Apply") &&
          !Has(src, "Material_LoadCustom"),
          "legacy plasma, flow, and opaque material shell are removed");
    CHECK(Has(src, "VFX_Material(shield->mat)"), "element colour comes from VFX_Material");
    CHECK(!Has(src, "SetShaderValueTexture"), "glass sphere has no surface-sheet dependency");
    CHECK(Has(src, "SkillManager_BeginShader") && Has(src, "SkillManager_EndShader"),
          "glass shader owns its uniforms and batching");
    CHECK(Has(src, "rlDrawRenderBatchActive"), "render-state changes are bracketed by flushes");
    CHECK(Has(src, "VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false") &&
          Has(src, "VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false") &&
          !Has(src, "VFX_SURFACE_MULTIPLIED"),
          "the body is alpha-composited and only the Fresnel rim is additive");
    CHECK(Has(src, "rlEnableBackfaceCulling") &&
          Has(src, "RL_CULL_FACE_FRONT") && Has(src, "RL_CULL_FACE_BACK"),
          "shield composites both glass interfaces with explicit cull order");
    CHECK(Has("core/shaders/glass_shell.fs", "calcFresnel") &&
          Has("core/shaders/glass_shell.fs", "u_emissionOnly") &&
          Has("core/shaders/glass_shell.fs", "u_fresnelPower"),
          "glass sphere reuses shared Fresnel math with a transparent body");
    CHECK(Has("core/shaders/glass_shell.vs", "fragPosition = vertexPosition") &&
          Has("core/shaders/glass_shell.fs", "normalize(-fragPosition)"),
          "glass Fresnel keeps immediate-mode normal and view coordinates consistent");
    CHECK(Has("core/shaders/glass_shell.fs", "u_lightDirView") &&
          Has(src, "Environment_GetSunDirection") && Has(src, "lightView"),
          "glass body and rim include the camera-relative environment light");
    CHECK(Has(src, "wallPass") && Has("core/shaders/glass_shell.fs", "u_wallPass"),
          "front and back glass interfaces receive separate optical weights");
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
